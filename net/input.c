#include "ns.h"
#include <inc/lib.h>

extern union Nsipc nsipcbuf;


void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.

	int r, i;
	while(1){
		while ((r = sys_net_recv(&nsipcbuf))){
			if (r == -E_RX_EMPTY){
				sys_yield();
			}
			else {
				break;
			}
		} 
		if (r < 0){
			panic("input error: sys_net_recv returned: %e\n", r);
		}
		ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_P | PTE_U);
		// unmap page so kernel can reuse this page if no other envs ref. it
		//sys_page_unmap(curenv->env_id, _pkt);
		//wait for the page to be copied, maybe do this for #CPU's 
		sys_yield();
	}
}
