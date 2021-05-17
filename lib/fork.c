// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
    uint32_t page_num = PGNUM(addr);
    uint32_t old_perms = uvpt[page_num] & PTE_SYSCALL;
    if ((err & FEC_WR) != FEC_WR
        // checks if the page exists and if it is COW
        || (old_perms | PTE_COW | PTE_P) != old_perms) {
        panic("[%08x] user fault va %08x ip %08x\n",
            curenv->env_id, addr, utf->utf_eip);
    }

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
    void * aligned_addr = ROUNDDOWN(addr, PGSIZE);

    uint32_t new_perms = (old_perms | PTE_W) & ~PTE_COW;
    r = sys_page_alloc(curenv->env_id, PFTEMP, new_perms);
    if (r<0) {
        panic("unable to allocate copy of COW page: %e", r);
    }

    memmove(PFTEMP, aligned_addr, PGSIZE);

    r = sys_page_map(curenv->env_id, PFTEMP, curenv->env_id,
                    aligned_addr, new_perms);
    if (r < 0) {
        panic("unable to map copy of COW page: %e", r);
    }

    r = sys_page_unmap(curenv->env_id, PFTEMP);
    if (r < 0) {
        panic("unable to unmap temp copy of COW page: %e", r);
    }
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
    uint32_t perm = uvpt[pn] & PTE_SYSCALL;
    if ((perm & PTE_P) == 0) {
        return -E_INVAL;
    }
    if ((perm & PTE_SHARE) != 0){
        if ((r = sys_page_map(curenv->env_id, (void*)(pn*PGSIZE),
                    envid, (void*)(pn*PGSIZE), perm)) < 0){
            return r;
        }
        return 0;
    }
    if ((perm & (PTE_W | PTE_COW)) != 0) {
        perm = (perm | PTE_COW) & ~PTE_W;
    }
    // set the page of the child env COW
    r = sys_page_map(curenv->env_id, (void*)(pn*PGSIZE),
                    envid, (void*)(pn*PGSIZE), perm);
    if (r<0) {
        return r;
    }

    // set the page of the parent COW
    r = sys_page_map(curenv->env_id, (void*)(pn*PGSIZE),
                    curenv->env_id, (void*)(pn*PGSIZE), perm);
    if (r<0) {
        return r;
    }

	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.

    uint8_t *addr;
	int r;
    int i;
    envid_t parent_envid = curenv->env_id;

    uint32_t except_stack_num = PGNUM(UXSTACKTOP-PGSIZE);

    set_pgfault_handler(pgfault);

    envid_t child_envid = sys_exofork();

    if (child_envid < 0) {
        return child_envid;
    }

    if (child_envid == 0) {
		// this is executed in the child

        // set thisenv to the child env
		thisenv = curenv;
		return 0;
	}

    // this is executed in the parent

    // mark every writeable page in the address space of both envs as COW
    // other than the exception stack
    int pgdir_index, pgtable_index, page_number;
    for (pgdir_index = 0; pgdir_index< NPDENTRIES; pgdir_index++) {
        if ((uvpd[pgdir_index] | PTE_P | PTE_U) != uvpd[pgdir_index]) {
            // skip unmapped pages in page directory
            continue;
        }

        for (pgtable_index=0; pgtable_index< NPTENTRIES; pgtable_index++) {
            void *page_addr = PGADDR(pgdir_index, pgtable_index, 0);
            if ((uintptr_t)page_addr >= UTOP) {
                // skip kernel pages
                continue;
            }

            uint32_t page_num = PGNUM(page_addr);
            if ((uvpt[page_num] | PTE_P | PTE_U) != uvpt[page_num]) {
                // skip unmapped pages in each page table
                continue;
            }

            if (page_num == except_stack_num) {
                // dont remap exception stack
                continue;
            }

            r = duppage(child_envid, page_num);
            if (r<0) {
                sys_env_destroy(child_envid);
                return r;
            }
        }
    }

    // setup the page fault handler and allocate exception stack for child
    r = sys_page_alloc(child_envid, (void*)(UXSTACKTOP-PGSIZE),
                        PTE_U | PTE_W | PTE_P);
    if (r<0) {
        sys_env_destroy(child_envid);
        return r;
    }
    r = sys_env_set_pgfault_upcall(child_envid, curenv->env_pgfault_upcall);
    if (r<0) {
        sys_env_destroy(child_envid);
        return r;
    }

    // start running the child env
    r = sys_env_set_status(child_envid, ENV_RUNNABLE);
    if (r<0) {
        sys_env_destroy(child_envid);
        return r;
    }

    return child_envid;
}

// Challenge!
int
sfork(void)
{

    uint8_t *addr;
	int r;
    int i;
    envid_t parent_envid = curenv->env_id;

    uint32_t except_stack_num = PGNUM(UXSTACKTOP-PGSIZE);

    set_pgfault_handler(pgfault);

    envid_t child_envid = sys_exofork();

    if (child_envid < 0) {
        return child_envid;
    }

    if (child_envid == 0) {
		// this is executed in the child

        // dont "fix" thisenv, since both envs share it
		return 0;
	}

    // this is executed in the parent

    int pgdir_index, pgtable_index, page_number;
    for (pgdir_index = 0; pgdir_index< NPDENTRIES; pgdir_index++) {
        if ((uvpd[pgdir_index] | PTE_P | PTE_U) != uvpd[pgdir_index]) {
            // skip unmapped pages in page directory
            continue;
        }

        for (pgtable_index=0; pgtable_index< NPTENTRIES; pgtable_index++) {
            void *page_addr = PGADDR(pgdir_index, pgtable_index, 0);
            if ((uintptr_t)page_addr >= UTOP) {
                // skip kernel pages
                continue;
            }

            uint32_t page_num = PGNUM(page_addr);
            if ((uvpt[page_num] | PTE_P | PTE_U) != uvpt[page_num]) {
                // skip unmapped pages in each page table
                continue;
            }

            if (page_num == except_stack_num) {
                // dont remap exception stack,
                // to prevent races between envs on page faults
                continue;
            }

            uint32_t perm = uvpt[page_num] & PTE_SYSCALL;

            // share the mapping between child and parent
            r = sys_page_map(parent_envid, page_addr, child_envid, page_addr, perm);

            if (r<0) {
                sys_env_destroy(child_envid);
                return r;
            }
        }
    }

    // mark the stack as a COW page for both envs
    uint32_t stack_num = PGNUM(USTACKTOP-PGSIZE);
    duppage(child_envid, stack_num);

    // setup the page fault handler and allocate exception stack for child
    r = sys_page_alloc(child_envid, (void*)(UXSTACKTOP-PGSIZE),
                        PTE_U | PTE_W | PTE_P);
    if (r<0) {
        sys_env_destroy(child_envid);
        return r;
    }
    r = sys_env_set_pgfault_upcall(child_envid, curenv->env_pgfault_upcall);
    if (r<0) {
        sys_env_destroy(child_envid);
        return r;
    }

    // start running the child env
    r = sys_env_set_status(child_envid, ENV_RUNNABLE);
    if (r<0) {
        sys_env_destroy(child_envid);
        return r;
    }

    return child_envid;
}
