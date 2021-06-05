#include <inc/string.h>
#include "inc/error.h"
#include <kern/e1000.h>
#include <kern/pmap.h>

typedef uint32_t reg_t;

#define TX_DESC_COUNT 16

// ROUNDUP/DOWN cant be used as an expression
#undef ROUNDDOWN
#undef ROUNDUP
#define ROUNDDOWN(a,n) ((uint32_t)(a) - ((uint32_t)(a) % (uint32_t)(n)))
#define ROUNDUP(a,n) ROUNDDOWN(a + n - 1, n)

#define TX_BUFF_SIZE  ROUNDUP(MAX_PACKET_SIZE, PGSIZE)

// calculates the number of unused registers,
// between two byte offsets
#define UNUSED_BETWEEN(REG1, REG2) ((REG2 - REG1 - sizeof(reg_t)) / sizeof(reg_t))

// defines a new MMIO register field with the given type, name and offset,
// and pads the structure untill the next used register
#define ADD_REG(name, offset, next) reg_t name; \
    reg_t _unused_after_##name[UNUSED_BETWEEN(offset, next)];

#define E1000_CTRL      0x00000
#define E1000_STATUS    0x00008
#define E1000_TCTL      0x00400
#define E1000_TIPG      0x00410
#define E1000_TDBAL     0x03800
#define E1000_TDBAH     0x03804
#define E1000_TDLEN     0x03808
#define E1000_TDH       0x03810
#define E1000_TDT       0x03818

// TCTL Register
#define TCTL_EN         (1 << 1)    // Transmit Enable
#define TCTL_PSP        (1 << 3)    // Pad Short Packets
#define TCTL_CT_SHIFT   4           // Collision Threshold
#define TCTL_COLD_SHIFT 12          // Collision Distance
#define TCTL_SWXOFF     (1 << 22)   // Software XOFF Transmission

// TCTL Register setup values
#define TCTL_CT         (0x10 << TCTL_CT_SHIFT)
#define TCTL_COLD       (0x40 << TCTL_COLD_SHIFT)

// TIPG Register
#define TIPG_IPGT_SHIFT     0       // IPG Transmit Time
#define TIPG_IPGR1_SHIFT    10      // IPG Receive Time 1
#define TIPG_IPGR2_SHIFT    20      // IPG Receive Time 2

// TIPG Register setup values
// based on section 13.4.34 of the Developer's Manual
//
// this section contains a contradition
// since the value of IPGR2 if 6, 2/3 of which is 4, not 8.
//
// the answer is given by cross-referencing with Intel Ethernet Controller I217 datasheet,
// which states that for IEEE 802.3 compliance, IPGR1 should be 2/3 of the total effective IPG
// which is 12 once the additional 6 MAC clocks used by the MAC are added to IPGR2.
#define TIPG_IPGT           (10 << TIPG_IPGT_SHIFT)
#define TIPG_IPGR1          (8 << TIPG_IPGR1_SHIFT)
#define TIPG_IPGR2          (20 << TIPG_IPGR2_SHIFT)

// Trasmission status
#define TX_STATUS_DD        1           // Descriptor Done
#define TX_STATUS_EC        (1 << 1)    // Excess Collisions
#define TX_STATUS_LC        (1 << 2)    // Late Collision
#define TX_STATUS_TU        (1 << 3)    // Transmit Underrun

// Trasmission Command
#define TX_CMD_EOP       1           // End Of Packet
#define TX_CMD_IFCS      (1 << 1)    // Insert FCS
#define TX_CMD_IC        (1 << 2)    // Insert Checksum
#define TX_CMD_RS        (1 << 3)    // Report Status
#define TX_CMD_RSV       (1 << 4)    // Report Packet Sent
#define TX_CMD_DEXT      (1 << 5)    // Extension (0 for legacy mode)
#define TX_CMD_VLE       (1 << 6)    // VLAN Packet Enable
#define TX_CMD_IDE       (1 << 7)    // Interrupt Delay Enable

struct e1000_regs {
    // Device Control - RW
    ADD_REG(ctrl, E1000_CTRL, E1000_STATUS)
    // Device Status - RO
    ADD_REG(status, E1000_STATUS, E1000_TCTL)
    // TX Control - RW
    ADD_REG(tctl, E1000_TCTL, E1000_TIPG)
    // TX Inter-packet gap -RW
    ADD_REG(tipg, E1000_TIPG, E1000_TDBAL)
    // TX Descriptor Base Address Low - RW
    ADD_REG(tdbal, E1000_TDBAL, E1000_TDBAH)
    // TX Descriptor Base Address High - RW
    ADD_REG(tdbah, E1000_TDBAH, E1000_TDLEN)
    // TX Descriptor Length - RW
    ADD_REG(tdlen, E1000_TDLEN, E1000_TDH)
    // TX Descriptor Head - RW
    ADD_REG(tdh, E1000_TDH, E1000_TDT)
    // TX Descripotr Tail - RW
    ADD_REG(tdt, E1000_TDT, E1000_TDT + sizeof(reg_t))
} __attribute__ ((packed));

struct tx_status {
    // Descriptor Done
    uint8_t dd : 1;
    // Excess Collisions
    uint8_t ec : 1;
    // Late Collision
    uint8_t lc : 1;
    // Transmit Underrun
    uint8_t tu : 1;
};

struct tx_desc
{
        uint64_t addr;
        uint16_t length;
        uint8_t cso;
        uint8_t cmd;
        uint8_t status;
        uint8_t css;
        uint16_t special;
} __attribute__ ((packed));

volatile struct e1000_regs *e1000_reg_mem;

struct tx_desc tx_desc_list[TX_DESC_COUNT];
uint8_t tx_buffers[TX_DESC_COUNT][TX_BUFF_SIZE];

// LAB 6: Your driver code here
int e1000_attach(struct pci_func *pcif) {
    pci_func_enable(pcif);

    // map network card registores to memory
    e1000_reg_mem = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);

    // setup transmission ring buffer
    e1000_reg_mem->tdbal = (reg_t)va2pa(kern_pgdir, tx_desc_list);
    e1000_reg_mem->tdbah = 0;
    e1000_reg_mem->tdlen = TX_DESC_COUNT * sizeof(struct tx_desc);
    e1000_reg_mem->tdh = 0;
    e1000_reg_mem->tdt = 0;

    // setup transmission settings
    e1000_reg_mem->tctl |= TCTL_EN;
    e1000_reg_mem->tctl |= TCTL_PSP;
    e1000_reg_mem->tctl |= TCTL_CT;
    e1000_reg_mem->tctl |= TCTL_COLD;

    // setup transmission IPG time
    e1000_reg_mem->tipg |= TIPG_IPGT;
    e1000_reg_mem->tipg |= TIPG_IPGR1;
    e1000_reg_mem->tipg |= TIPG_IPGR2;

    // mark transmission descriptors as available
    int i;
    for (i=0; i < TX_DESC_COUNT; i++) {
        tx_desc_list[i].status = TX_STATUS_DD;
        tx_desc_list[i].addr = 0;
        tx_desc_list[i].cmd = 0;
    }
    return true;
}

// takes an address to the packet data, and builds up a packet from it
// if end_packet is true, interprets the incoming data as the last part of the packet
// and transmits it over the network.
// returns 0 on success, -E__NO_MEM if the transmit queue is full.
int transmit_packet(void *addr, size_t length, bool end_packet) {
    size_t cur_index = e1000_reg_mem->tdt;
    struct tx_desc *tail = &tx_desc_list[cur_index];
    if (tail->status & TX_STATUS_DD) {
        memcpy(tx_buffers[cur_index], addr, length);
        tail->cmd |= TX_CMD_RS;
        tail->cmd |= end_packet ? TX_CMD_EOP : 0;
        tail->status = 0;
        tail->addr = (uint64_t)va2pa(kern_pgdir, tx_buffers[cur_index]);
        tail->length = (uint16_t)length;
        e1000_reg_mem->tdt = (cur_index + 1) % TX_DESC_COUNT;
        return 0;
    } else {
        return -E_NO_MEM;
    }
}