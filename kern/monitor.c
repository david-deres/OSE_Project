// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display a backtrace of the current function", mon_backtrace },
	{ "vmmap",
"Modify the mapping of the virtual memory with the following arguments:\n\
set {vstart} {vend} {pstart} {pend} - map the virtual pages vstart-vend\n\
                                      to physical pages pstart-pend\n\
clear {vstart} {vend} - clear the mapping of virtual pages vstart-vend\n\
perm {vstart} {vend} [R/RW/RU/RWU] - set the permissions\n\
    of the virtual pages, where R is read-only, RW is for read-write\n\
    and U can be added to any of them to indicate user access\n\
show {vstart} {vend} - show the mapping and permissions\n\
    for pages contating virtual addresses vstart-vend\n\
dump [v/p] {start} {end}- dump the contents of the addresses start-end\n\
    those are interpreted as virtual with 'v' or physical with 'p'",
mon_vmmap },
};

#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t ebp = read_ebp();
	cprintf("Stack backtrace:\n");
	while (ebp != 0) {
		// the return address is stored after the previous ebp
		uintptr_t eip = *((uintptr_t*)ebp+1);
		struct Eipdebuginfo debug_info;

		debuginfo_eip(eip, &debug_info);

		cprintf("ebp %x  eip %x  args", ebp, eip);
		int i;
		for (i=0; i<5; i++) {
			uint32_t arg = *((uint32_t*)ebp + 2 + i);
			cprintf(" %08x", arg);
		}
		cprintf("\n");
		cprintf("         %s:%d: %.*s+%d\n",
				debug_info.eip_file,
				debug_info.eip_line,
				debug_info.eip_fn_namelen,
				debug_info.eip_fn_name,
				eip - debug_info.eip_fn_addr);
		ebp = *(uint32_t*)ebp;
	}
	return 0;
}

// given an array of 2 strings constucts a memory range
//
// on success returns true and places the result in `range`
bool create_range(char **argv, MemoryRange *range, AddressType type) {
	char *endptr;
	uintptr_t start = strtol(argv[0], &endptr, 0);
	if (*endptr != '\0') {
		cprintf("got invalid memory address \"%s\"\n", argv[0]);
		return false;
	}
	uintptr_t end = strtol(argv[1], &endptr, 0);
	if (*endptr != '\0') {
		cprintf("got invalid memory address \"%s\"\n", argv[1]);
		return false;
	}

	if (end < start) {
		cprintf("got invalid address 0x%x-0x%x\n", start, end);
		return false;
	}

	*range = (MemoryRange) {start, end, type};
	return true;
}

void mon_vmmap_set(int argc, char **argv) {
	if (argc < 4) {
		cprintf("not enough arguments for `vmmap set`\n");
		return;
	}
}

void mon_vmmap_clear(int argc, char **argv) {
	if (argc < 2) {
		cprintf("not enough arguments for `vmmap clear`\n");
		return;
	}

	MemoryRange range;

	if (!create_range(argv, &range, VIRTUAL)) {
		return;
	}

	clear_pages(range);
}

void mon_vmmap_perm(int argc, char **argv) {
	if (argc < 3) {
		cprintf("not enough arguments for `vmmap perm`\n");
		return;
	}

	MemoryRange range;

	if (!create_range(argv, &range, VIRTUAL)) {
		return;
	}

	if (strcmp(argv[2], "R") == 0) {
		change_page_perm(range, 0);
	} else if (strcmp(argv[2], "RW") == 0) {
		change_page_perm(range, PTE_W);
	} else if (strcmp(argv[2], "RU") == 0) {
		change_page_perm(range, PTE_U);
	} else if (strcmp(argv[2], "RWU") == 0) {
		change_page_perm(range, PTE_W | PTE_U);
	} else {
		cprintf("got invalid memory permission \"%s\"\n", argv[2]);
		return;
	}
}

void mon_vmmap_show(int argc, char **argv) {
	if (argc < 2) {
		cprintf("not enough arguments for `vmmap show`\n");
		return;
	}

	MemoryRange range;

	if (!create_range(argv, &range, VIRTUAL)) {
		return;
	}

	show_pages(range);
}

void mon_vmmap_dump(int argc, char **argv) {
	if (argc < 3) {
		cprintf("not enough arguments for `vmmap dump`\n");
		return;
	}

	AddressType type;
	if (strcmp(argv[0], "p") == 0) {
		type = PHYSICAL;
	} else if (strcmp(argv[0], "v") == 0) {
		type = VIRTUAL;
	} else {
		cprintf("got invalid address type \"%s\"\n", argv[0]);
		return;
	}

	MemoryRange range;
	if (!create_range(argv+1, &range, type)) {
		return;
	}

	dump_range(range);
}

int
mon_vmmap(int argc, char **argv, struct Trapframe *tf) {
	if (argc < 2) {
		cprintf("not enough arguments for `vmmap`\n");
		return 0;
	}
	char *subcommand = argv[1];
	char **subargs = argv + 2;
	if (strcmp(subcommand, "set") == 0) {
		mon_vmmap_set(argc - 2, subargs);
	} else if (strcmp(subcommand, "clear") == 0) {
		mon_vmmap_clear(argc - 2, subargs);
	} else if (strcmp(subcommand, "perm") == 0) {
		mon_vmmap_perm(argc - 2, subargs);
	} else if (strcmp(subcommand, "show") == 0) {
		mon_vmmap_show(argc - 2, subargs);
	} else if (strcmp(subcommand, "dump") == 0) {
		mon_vmmap_dump(argc - 2, subargs);
	} else {
		cprintf("Unknown subcommand for `vmmap`\n");
	}
	return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
