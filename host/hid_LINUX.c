#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <usb.h>

#include "hid.h"

// ���� ��� HID ��⸦ �����ϱ� ���� ���� ����Ʈ
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

// �� ���� �ȿ����� ���Ǵ� private function��
static void add_hid(hid_t *h);
static hid_t * get_hid(int num);
static void free_all_hid(void);
static void hid_close(hid_t *hid);
static int hid_parse_item(uint32_t *val, uint8_t **data, const uint8_t *end);

// rawhid_recv(): USB ��Ŷ �б� �Լ�
// �Է�: USB ��� ��ȣ, ����, ���� ũ��, Ÿ�Ӿƿ�
// ���: ���� ���� ��Ŷ ũ�� (����: -1)
int rawhid_recv(int num, void *buf, int len, int timeout)
{
    hid_t *hid;
    int r;

    hid = get_hid(num);
    if (!hid || !hid->open) return -1;
    
    // ������ usb.h ����� �����Ǿ� �ִ� usb_interrupt_read() �Լ� ���
    r = usb_interrupt_read(hid->usb, hid->ep_in, buf, len, timeout);
    if (r >= 0) return r;
    if (r == -110) return 0; // timeout
    return -1;
}

// rawhid_send(): USB ��Ŷ ���� �Լ�
// �Է�: USB ��� ��ȣ, ����, ���� ũ��, Ÿ�Ӿƿ�
// ���: ������ ��Ŷ ũ�� (����: -1)
int rawhid_send(int num, void *buf, int len, int timeout)
{
    hid_t *hid;

    hid = get_hid(num);
    if (!hid || !hid->open) return -1;

    if (hid->ep_out) {
        // ������ usb.h ����� �����Ǿ� �ִ� usb_interrupt_write() �Լ� ���
        return usb_interrupt_write(hid->usb, hid->ep_out, buf, len, timeout);
    } else {
        return usb_control_msg(hid->usb, 0x21, 9, 0, hid->iface, buf, len, timeout);
    }
}

// rawhid_open(): HID ��� ���� �Լ�
// �Է�: �ִ� ��� ����, Vendor ID (�ƹ��ų�: -1), Product ID (�ƹ��ų�: -1), Usage Page (�ƹ��ų�: -1), Usage (�ƹ��ų�: -1)
// ���: ���� HID ��� ����
int rawhid_open(int max, int vid, int pid, int usage_page, int usage)
{
    struct usb_bus *bus;
    struct usb_device *dev;
    struct usb_interface *iface;
    struct usb_interface_descriptor *desc;
    struct usb_endpoint_descriptor *ep;

    usb_dev_handle *u;
    uint8_t buf[1024], *p;
    int i, n, len, tag, ep_in, ep_out, count = 0, claimed;
    uint32_t val = 0, parsed_usage, parsed_usage_page;
    hid_t *hid;

    if (first_hid) free_all_hid();
    printf("rawhid_open, max = %d\n", max);
    if (max < 1) return 0;
    usb_init();
    usb_find_busses();
    usb_find_devices();

    for (bus = usb_get_busses(); bus; bus = bus->next) {
        // ����Ǿ� �ִ� ��� USB ��ġ�� �ϳ��� Ȯ���ϸ�
        for (dev = bus->devices; dev; dev = dev->next) {
            // ������ Vendor ID�� Product ID�� ������ ��ġ�� ã��
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
            // ���� ����� ��� �������̽�(interface)�� Ȯ���ϸ�
            for (i = 0; i < dev->config->bNumInterfaces && iface; i++, iface++) {
                desc = iface->altsetting;
                if (!desc) continue;
                printf("interface #%d: class=%d, subclass=%d, protocol=%d\n",
                    i,
                    desc->bInterfaceClass,
                    desc->bInterfaceSubClass,
                    desc->bInterfaceProtocol);
                // �̸� ������ HID descriptor�� ��ġ�ϴ� ��쿡�� ó��
                if (desc->bInterfaceClass != 3) continue;
                if (desc->bInterfaceSubClass != 0) continue;
                if (desc->bInterfaceProtocol != 0) continue;

                ep = desc->endpoint;
                ep_in = ep_out = 0;
                // �ش� HID �������̽��� ��� ��������Ʈ(endpoint)�� Ȯ���ϸ�
                for (n = 0; n < desc->bNumEndpoints; n++, ep++) {
                    if (ep->bEndpointAddress & 0x80) {
                        if (!ep_in) ep_in = ep->bEndpointAddress & 0x7F;
                        printf("IN endpoint number: %d\n", ep_in);
                    } else {
                        if (!ep_out) ep_out = ep->bEndpointAddress;
                        printf("OUT endpoint %d\n", ep_out);
                    }
                }
                if (!ep_in) continue;
                
                // ������ usb.h ����� �����Ǿ� �ִ� usb_open() �Լ� ��� 
                if (!u) {
                    u = usb_open(dev);
                    if (!u) {
                        printf("unable to open device\n");
                        break;
                    }
                }
                printf("hid interface (generic)\n");
                
                // ����� �� �ִ��� Ȯ��
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
                
                // USB descriptor ���� �б�
                len = usb_control_msg(u, 0x81, 6, 0x2200, i, (char *)buf, sizeof(buf), 250);
                printf("descriptor, len=%d\n", len);
                if (len < 2) {
                    usb_release_interface(u, i);
                    continue;
                }

                // ������ ������ Usage Page �� Usage ID�� ��ġ�ϴ��� Ȯ��
                p = buf;
                parsed_usage_page = buf[0] + (buf[1] << 8);
                parsed_usage = buf[2] + (buf[3] << 8);
                if ((!parsed_usage_page) || (!parsed_usage) ||
                    (usage_page > 0 && parsed_usage_page != usage_page) || 
                    (usage > 0 && parsed_usage != usage)) {
                        usb_release_interface(u, i);
                        continue;
                }

                // HID ��ü �ʱ�ȭ
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


// rawhid_close(): HID ��� �Լ�
// �Է�: USB ��� ��ȣ
// ���: ����
void rawhid_close(int num)
{
    hid_t *hid;

    hid = get_hid(num);
    if (!hid || !hid->open) return;
    hid_close(hid);
}

// add_hid(): ���� ����Ʈ�� �ϳ��� HID ��ü �߰�
// �Է�: HID ��ü
// ���: ����
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

// get_hid(): ��ȣ�� HID ��ü ��������
// �Է�: HID ��ü ��ȣ
// ���: HID ��ü
static hid_t* get_hid(int num)
{
    hid_t *p;
    for (p = first_hid; p && num > 0; p = p->next, num--);
    return p;
}

// free_all_hid(): ��� HID ��ü �Ҵ� ����
// �Է�: ����
// ���: ����
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

// hid_close(): Ư�� HID ��ü �Ҵ� ����
// �Է�: HID ��ü
// ���: ����
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
