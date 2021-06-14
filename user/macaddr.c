#include <inc/lib.h>

void umain(int argc, char **argv) {
    uint8_t mac[6] = {};
    sys_get_mac_addr(mac);
    printf("the mac address of this device is: ");
    int i;
    for (i=0; i<6; i++) {
        printf("%02x", mac[i]);
        if (i != 5) {
            printf(":");
        }
    }
    printf("\n");
}