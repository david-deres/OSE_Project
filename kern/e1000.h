#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>

#define E1000_VENDOR_ID 0x8086
#define E1000_PRODUCT_ID 0x100E

int e1000_attach(struct pci_func *pcif);

// takes an address to the packet data, and transmits it to the network card
// returns 0 on success, and a negative value for an error
int transmit_packet(physaddr_t addr, size_t length);

#endif	// JOS_KERN_E1000_H
