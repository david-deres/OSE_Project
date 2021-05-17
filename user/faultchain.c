// test chaining multiple user-level fault handler

#include <inc/lib.h>

bool
handler1(struct UTrapframe *utf)
{
	void *addr = (void*)utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	cprintf("HANDLER 1: this shouldn't be reachable\n");
    return false;
}

bool
handler2(struct UTrapframe *utf)
{
	void *addr = (void*)utf->utf_fault_va;
	uint32_t err = utf->utf_err;
    int r;
    if ((r = sys_page_alloc(0, ROUNDDOWN(addr, PGSIZE),
				PTE_P|PTE_U|PTE_W)) < 0)
		panic("allocating at %x in page fault handler: %e", addr, r);

    cprintf("HANDLER 2: able to handle fault, stopping it here\n");
    return true;
}

bool
handler3(struct UTrapframe *utf)
{
	void *addr = (void*)utf->utf_fault_va;
	uint32_t err = utf->utf_err;
    cprintf("HANDLER 3: still unable to handle fault, passing it along\n");
    return false;
}

bool
handler4(struct UTrapframe *utf)
{
	void *addr = (void*)utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	cprintf("HANDLER 4: faulted at va %x, err %x\n", addr, err & 7);
    cprintf("HANDLER 4: unable to handle fault, passing it along\n");
    return false;
}

void
umain(int argc, char **argv)
{
	set_pgfault_handler(handler1);
    set_pgfault_handler(handler2);
    set_pgfault_handler(handler3);
    set_pgfault_handler(handler4);
	*(int*)0xDeadBeef = 0;
}