#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <usb.h>

#include "hid.h"

// 열린 모든 HID 기기를 관리하기 위한 연결 리스트
typedef struct hid_struct hid_t;
static hid_t *first_hid = NULL;
static hid_t *last_hid = NULL;
struct hid_struct {
    usb_dev_handle *usb;
    int open;
    int iface;
    int ep_in;
    int ep_out;
    struct hid_struct *prev;
    struct hid_struct *next;
};

// 본 파일 안에서만 사용되는 private function들
static void add_hid(hid_t *h);
static hid_t * get_hid(int num);
static void free_all_hid(void);
static void hid_close(hid_t *hid);

// rawhid_recv(): USB 패킷 읽기 함수
// 입력: USB 기기 번호, 버퍼, 버퍼 크기, 타임아웃
// 출력: 전달 받은 패킷 크기 (오류: -1)
int rawhid_recv(int num, void *buf, int len, int timeout)
{
    hid_t *hid;
    int r;

    hid = get_hid(num);
    if (!hid || !hid->open) return -1;
    
    // 리눅스 usb.h 헤더에 구현되어 있는 usb_interrupt_read() 함수 사용
    r = usb_interrupt_read(hid->usb, hid->ep_in, buf, len, timeout);
    if (r >= 0) return r;
    if (r == -110) return 0; // timeout
    return -1;
}

// rawhid_send(): USB 패킷 전송 함수
// 입력: USB 기기 번호, 버퍼, 버퍼 크기, 타임아웃
// 출력: 전송한 패킷 크기 (오류: -1)
int rawhid_send(int num, void *buf, int len, int timeout)
{
    hid_t *hid;

    hid = get_hid(num);
    if (!hid || !hid->open) return -1;

    if (hid->ep_out) {
        // 리눅스 usb.h 헤더에 구현되어 있는 usb_interrupt_write() 함수 사용
        return usb_interrupt_write(hid->usb, hid->ep_out, buf, len, timeout);
    } else {
        return usb_control_msg(hid->usb, 0x21, 9, 0, hid->iface, buf, len, timeout);
    }
}

// rawhid_open(): HID 기기 여는 함수
// 입력: 최대 기기 개수, Vendor ID (아무거나: -1), Product ID (아무거나: -1), Usage Page (아무거나: -1), Usage (아무거나: -1)
// 출력: 열린 HID 기기 개수
int rawhid_open(int max, int vid, int pid, int usage_page, int usage)
{
    struct usb_bus *bus;
    struct usb_device *dev;
    struct usb_interface *iface;
    struct usb_interface_descriptor *desc;
    struct usb_endpoint_descriptor *ep;

    usb_dev_handle *u;
    uint8_t buf[1024];
    int i, n, len, ep_in, ep_out, count = 0, claimed;
    uint32_t parsed_usage, parsed_usage_page;
    hid_t *hid;

    if (first_hid) free_all_hid();
    printf("rawhid_open, max = %d\n", max);
    if (max < 1) return 0;
    usb_init();
    usb_find_busses();
    usb_find_devices();

    for (bus = usb_get_busses(); bus; bus = bus->next) {
        // 연결되어 있는 모든 USB 장치를 하나씩 확인하며
        for (dev = bus->devices; dev; dev = dev->next) {
            // 설정한 Vendor ID와 Product ID를 가지는 장치를 찾기
            if (vid > 0 && dev->descriptor.idVendor != vid) continue;
            if (pid > 0 && dev->descriptor.idProduct != pid) continue;
            if (!dev->config) continue;
            if (dev->config->bNumInterfaces < 1) continue;
            printf("device: vid=%04X, pic=%04X, with %d iface\n",
                dev->descriptor.idVendor,
                dev->descriptor.idProduct,
                dev->config->bNumInterfaces);
            iface = dev->config->interface;
            u = NULL;
            claimed = 0;
            // 현재 기기의 모든 인터페이스(interface)를 확인하며
            for (i = 0; i < dev->config->bNumInterfaces && iface; i++, iface++) {
                desc = iface->altsetting;
                if (!desc) continue;
                printf("interface #%d: class = %d, subclass = %d, protocol = %d\n",
                    i,
                    desc->bInterfaceClass,
                    desc->bInterfaceSubClass,
                    desc->bInterfaceProtocol);
                // 미리 정의한 HID descriptor와 일치하는 경우에만 처리
                if (desc->bInterfaceClass != 3) continue;
                if (desc->bInterfaceSubClass != 0) continue;
                if (desc->bInterfaceProtocol != 0) continue;

                ep = desc->endpoint;
                ep_in = ep_out = 0;
                // 해당 HID 인터페이스의 모든 엔드포인트(endpoint)를 확인하며
                for (n = 0; n < desc->bNumEndpoints; n++, ep++) {
                    if (ep->bEndpointAddress & 0x80) {
                        if (!ep_in) ep_in = ep->bEndpointAddress & 0x7F;
                        printf("IN endpoint number: %d\n", ep_in);
                    } else {
                        if (!ep_out) ep_out = ep->bEndpointAddress;
                        printf("OUT endpoint: %d\n", ep_out);
                    }
                }
                if (!ep_in) continue;
                
                // 리눅스 usb.h 헤더에 구현되어 있는 usb_open() 함수 사용 
                if (!u) {
                    u = usb_open(dev);
                    if (!u) {
                        printf("unable to open device\n");
                        break;
                    }
                }
                printf("hid interface (generic)\n");
                
                // 사용할 수 있는지 확인 (다른 드라이버가 사용 중이라면 detach 시도)
                if (usb_get_driver_np(u, i, (char *)buf, sizeof(buf)) >= 0) {
                    printf("in use by driver \"%s\"\n", buf);
                    if (usb_detach_kernel_driver_np(u, i) < 0) {
                        printf("unable to detach from kernel\n");
                        continue;
                    }
                }
                if (usb_claim_interface(u, i) < 0) {
                    printf("unable claim interface %d\n", i);
                    continue;
                }
                
                // USB descriptor 정보 읽기
                len = usb_control_msg(u, 0x81, 6, 0x2200, i, (char *)buf, sizeof(buf), 250);
                printf("descriptor, len=%d\n", len);
                if (len < 2) {
                    usb_release_interface(u, i);
                    continue;
                }

                // 사전에 설정한 Usage Page 및 Usage ID와 일치하는지 확인
                parsed_usage_page = buf[1] + (buf[2] << 8);
                parsed_usage = buf[3] + (buf[4] << 8);
                printf("parsed usage page = %d, parsed usage ID = %d\n", parsed_usage_page, parsed_usage);
                if ((!parsed_usage_page) || (!parsed_usage) ||
                    (usage_page > 0 && parsed_usage_page != usage_page) || 
                    (usage > 0 && parsed_usage != usage)) {
                        usb_release_interface(u, i);
                        continue;
                }

                // HID 객체 초기화
                hid = (struct hid_struct *)malloc(sizeof(struct hid_struct));
                if (!hid) {
                    usb_release_interface(u, i);
                    continue;
                }
                hid->usb = u;
                hid->iface = i;
                hid->ep_in = ep_in;
                hid->ep_out = ep_out;
                hid->open = 1;
                add_hid(hid);
                claimed++;
                count++;
                if (count >= max) return count;
            }
            if (u && !claimed) usb_close(u);
        }
    }
    return count;
}


// rawhid_close(): HID 기기 함수
// 입력: USB 기기 번호
// 출력: 없음
void rawhid_close(int num)
{
    hid_t *hid;

    hid = get_hid(num);
    if (!hid || !hid->open) return;
    hid_close(hid);
}

// add_hid(): 연결 리스트에 하나의 HID 객체 추가
// 입력: HID 객체
// 출력: 없음
static void add_hid(hid_t *h)
{
    if (!first_hid || !last_hid) {
        first_hid = last_hid = h;
        h->next = h->prev = NULL;
        return;
    }
    last_hid->next = h;
    h->prev = last_hid;
    h->next = NULL;
    last_hid = h;
}

// get_hid(): 번호로 HID 객체 가져오기
// 입력: HID 객체 번호
// 출력: HID 객체
static hid_t* get_hid(int num)
{
    hid_t *p;
    for (p = first_hid; p && num > 0; p = p->next, num--);
    return p;
}

// free_all_hid(): 모든 HID 객체 할당 해제
// 입력: 없음
// 출력: 없음
static void free_all_hid(void)
{
    hid_t *p, *q;

    for (p = first_hid; p; p = p->next) {
        hid_close(p);
    }
    p = first_hid;
    while (p) {
        q = p;
        p = p->next;
        free(q);
    }
    first_hid = last_hid = NULL;
}

// hid_close(): 특정 HID 객체 할당 해제
// 입력: HID 객체
// 출력: 없음
static void hid_close(hid_t *hid)
{
    hid_t *p;
    int others=0;

    usb_release_interface(hid->usb, hid->iface);
    for (p = first_hid; p; p = p->next) {
        if (p->open && p->usb == hid->usb) others++;
    }
    if (!others) usb_close(hid->usb);
    hid->usb = NULL;
}
