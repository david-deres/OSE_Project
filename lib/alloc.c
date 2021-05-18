#include <inc/lib.h>

// a basic allocator

#define MAX_ALLOC PGSIZE
#define START_ALLOC (ROUNDUP((uintptr_t)end, PGSIZE) + PGSIZE)
#define ALLOC_PAGE(i) ((void*)(START_ALLOC + i*PGSIZE))
#define ALLOC_INDEX(addr) (((uintptr_t)ROUNDDOWN(addr, PGSIZE) - START_ALLOC) / PGSIZE)

extern char end[];

// allocates an entire page at once,
// and at most MAX_ALLOC pages can be allocated at any given time

static bool allocations[MAX_ALLOC];

// returns a pointer to the first available page on success
// returns NULL on failure to allocate
void *malloc() {
    int i;
    for (i = 0; i < MAX_ALLOC; i++) {
        if (allocations[i] == false) {
            int r = sys_page_alloc(curenv->env_id, ALLOC_PAGE(i),
                                   PTE_U | PTE_P | PTE_W);
            if (r < 0) {
                return NULL;
            }
            allocations[i] = true;
            return ALLOC_PAGE(i);
        }
    }
    return NULL;
}

void free(void *page) {
    if ((uintptr_t)page < START_ALLOC ||
        (uintptr_t)page >= START_ALLOC + MAX_ALLOC * PGSIZE) {
        panic("freeing page not from allocation pool");
    }
    allocations[ALLOC_INDEX(page)] = false;
}