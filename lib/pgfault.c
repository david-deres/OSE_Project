// User-level page fault handler support.
// Rather than register the C page fault handler directly with the
// kernel as the page fault handler, we register the assembly language
// wrapper in pfentry.S, which in turns calls the registered C
// function.

#include <inc/lib.h>


// Assembly language pgfault entrypoint defined in lib/pfentry.S.
extern void _pgfault_upcall(void);

// Pointer to currently installed C-language pgfault handler.
void (*_pgfault_handler)(struct UTrapframe *utf);

#define MAX_HANDLERS 128

// a ringbuffer of page fault handlers
// this is an append-only buffer
// so existing handlers as simply all the non-NULL slots
struct {
    pgfault_handler_t handlers[MAX_HANDLERS];
    // an index to the next slot that would be filled in an append
    size_t next;
} typedef HandlerRing;

HandlerRing handler_ring;

// sets a new handler to the end of the ringbuffer,
// overwriting the first handler if it ran out of slots
static void append_handler(pgfault_handler_t handler) {
    handler_ring.handlers[handler_ring.next] = handler;
    handler_ring.next = (handler_ring.next + 1) % MAX_HANDLERS;
}

static void execute_handlers(struct UTrapframe *utf) {
    // start from the last handler set
    // and continue backwards untill all have been tried
    size_t index = (handler_ring.next - 1) % MAX_HANDLERS;
    int attempts;
    for (attempts = 0; attempts < MAX_HANDLERS; attempts++) {
        pgfault_handler_t cur_handler = handler_ring.handlers[index];
        if (cur_handler != NULL) {
            if (cur_handler(utf)) {
                return;
            }
        }
        index = (MAX_HANDLERS-1 + index)%MAX_HANDLERS;
    }

    // no handler caught this page fault
    panic("[%08x] user fault va %08x ip %08x\n",
            curenv->env_id, utf->utf_fault_va, utf->utf_eip);
}

static void init_handler_ring() {
    memset(handler_ring.handlers, 0, MAX_HANDLERS * sizeof(pgfault_handler_t));
    handler_ring.next = 0;
}

//
// Set the page fault handler function.
// If there isn't one yet, _pgfault_handler will be 0.
// The first time we register a handler, we need to
// allocate an exception stack (one page of memory with its top
// at UXSTACKTOP), and tell the kernel to call the assembly-language
// _pgfault_upcall routine when a page fault occurs.
//
void
set_pgfault_handler(pgfault_handler_t handler)
{
	int r;

	if (_pgfault_handler == 0) {
		// First time through!
		// LAB 4: Your code here.

        r = sys_page_alloc(curenv->env_id, (void*)(UXSTACKTOP - PGSIZE),
                            PTE_P | PTE_W | PTE_U);
        if (r<0) {
            panic("unable to allocate exception stack: %e", r);
        }

        r = sys_env_set_pgfault_upcall(curenv->env_id, _pgfault_upcall);
        if (r<0) {
            panic("unable to set page fault handler: %e", r);
        }

        init_handler_ring();
        _pgfault_handler = execute_handlers;
	}

	// Save handler pointer for assembly to call.
	append_handler(handler);
}
