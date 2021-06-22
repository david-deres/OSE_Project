#include "ns.h"
#include <inc/lib.h>

extern union Nsipc nsipcbuf;

// block the thread untill the packet has been sent to the driver
static int send_packet(void *buffer, size_t length, bool isEOP) {
    int r = -E_RX_FULL;
    while (r == -E_RX_FULL) {
        r = sys_net_try_send(buffer, length, isEOP);
        if (r == -E_RX_FULL){
            sys_yield();
        }
    }
    return r;
}

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";
    struct pgvec buffers;
    int lenghts[16];
    void *pg_addr[16] = {NULL};
    buffers.data_len = (void*)&lenghts;
    buffers.pgv_base = pg_addr;

    while (true) {
        int perm = 0;
        void *va = &nsipcbuf.pkt.jp_len;
        envid_t whom = 0;
        int req_type = ipc_recv(&whom, va, &perm);
        if (req_type < 0) {
            panic("receiving IPC on output env failed: %d\n", req_type);
        }

        if (req_type == NSREQ_OUTPUT){
            if (!(perm & PTE_P)) {
			    panic("buffer missing from message to output env\n");
		    }
            send_packet(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len, true);
            sys_page_unmap(curenv->env_id, &nsipcbuf.pkt);
        }
        else if (req_type == NSREQ_OUTPUT_MULTI) {
            int j;
            for (j = 0; j < 16; j++){
                lenghts[j] = 0;
            }
            int num_of_pkts = ipc_recv_multi(&buffers, &perm);
            if (num_of_pkts <= 0){
                    panic("receiving multiple IPC on output env failed: %e\n", num_of_pkts);
            }
            if (!(perm & PTE_P)) {
                panic("buffer missing from message to output env\n");
            }  
            int i;
            for (i=0; i < num_of_pkts - 1; i++){
                send_packet(buffers.pgv_base[i], lenghts[i], false);
            }
            send_packet(buffers.pgv_base[i],lenghts[i], true);
            for (i=0; i < num_of_pkts; i++){
                sys_page_unmap(curenv->env_id, (void *)ROUNDDOWN(buffers.pgv_base[i], PGSIZE));
            }
        }
        else{
            panic("unexpected message type for output env\n");
        }
    }
}
