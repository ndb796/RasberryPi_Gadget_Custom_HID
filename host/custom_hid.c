#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <termios.h>
#include "hid.h"

int main()
{
    int i, r, num;
    char c, buf[64];

    // 1D6B:0104:FFAB:7777
    r = rawhid_open(1, 0x1D6B, 0x0104, 0xFFAB, 0x7777);
    if (r <= 0) {
        r = rawhid_open(1, 0x1D6B, 0x0104, 0xFFAB, 0x7777);
        if (r <= 0) {
            printf("no rawhid device found\n");
            return -1;
        }
    }
    printf("found rawhid device\n");

    while (1) {
        // Raw HID 패킷이 도착했는지 확인 
        num = rawhid_recv(0, buf, 64, 220);
        if (num < 0) {
            printf("\nerror reading, device went offline\n");
            rawhid_close(0);
            return 0;
        }
        if (num > 0) {
            printf("\nrecv %d bytes:\n", num);
            for (i = 0; i < num; i++) {
                printf("%02X ", buf[i] & 255);
                if (i % 16 == 15 && i < num - 1) printf("\n");
            }
            printf("\n");
            // 전달 받은 이후에 의미 없는 64 바이트 패킷 전송
			for (i = 0; i < 64; i++) {
				buf[i] = 'F';
			}
			rawhid_send(0, buf, 64, 100);
        }
	}
}
