#include "ns.h"
#include <inc/lib.h>

extern union Nsipc nsipcbuf;

#define BUFFER_SIZE 2048




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
	uint8_t input_buffer[BUFFER_SIZE];

	while(1){
		size_t* len = 0;
		void* buff = &nsipcbuf.pkt.jp_data;
		void *va = &nsipcbuf.pkt.jp_len;
		memset(input_buffer, 0, BUFFER_SIZE);
		if ((r = sys_net_recv(input_buffer, len))<0){
			panic("input error: %e\n", r);
		}
		if (len<0){
			panic("input error: received invalid length\n");
		}
		memcpy(buff, input_buffer, *len);
		nsipcbuf.pkt.jp_len = *len;
		ipc_send(ns_envid, NSREQ_INPUT, va, PTE_P | PTE_U);
		//not sure why
		//sys_page_unmap(curenv->env_id, &nsipcbuf.pkt);
		//wait for the page to be copied, maybe do this for #CPU's 
		sys_yield();
	}
}
