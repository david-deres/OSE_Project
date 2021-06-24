#include "ns.h"
#include <inc/lib.h>

extern union Nsipc nsipcbuf;

// block the thread untill the packet has been sent to the driver
static int send_packet(void *buffer, size_t length) {
    int r = -E_AGAIN;
    while (r == -E_AGAIN) {
        r = sys_net_try_send(buffer, length);
        if (r == -E_AGAIN){
            sys_yield();
        }
    }
    return r;
}

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
    while (true) {
        int perm = 0;
        void *va = &nsipcbuf.pkt.jp_len;
        envid_t whom = 0;
        int req_type = ipc_recv(&whom, va, &perm);

        if (req_type < 0) {
            panic("receiving IPC on output env failed: %e\n", req_type);
        }

        if (req_type != NSREQ_OUTPUT) {
            panic("unexpected message type for output env\n");
        }

        // All requests to output env must contain an argument page
		if (!(perm & PTE_P)) {
			panic("buffer missing from message to output env\n");
		}

        send_packet(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len);
        sys_page_unmap(curenv->env_id, &nsipcbuf.pkt);
    }
}
