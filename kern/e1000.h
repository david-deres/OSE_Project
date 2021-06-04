#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>

#define E1000_VENDOR_ID 0x8086
#define E1000_PRODUCT_ID 0x100E

int e1000_attach(struct pci_func *pcif);

// takes an address to the packet data, and builds up a packet from it
// if end_packet is true, interprets the incoming data as the last part of the packet
// and transmits it over the network.
// returns 0 on success, -E__NO_MEM if the transmit queue is full.
int transmit_packet(physaddr_t addr, size_t length, bool end_packet);

#endif	// JOS_KERN_E1000_H
