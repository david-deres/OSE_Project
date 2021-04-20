/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>



###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */

TRAPHANDLER_NOEC(div_error_h, T_DIVIDE)
TRAPHANDLER_NOEC(debug_h, T_DEBUG)
TRAPHANDLER_NOEC(nmi_h, T_NMI)
TRAPHANDLER_NOEC(breakpoint_h, T_BRKPT)
TRAPHANDLER_NOEC(overflow_h, T_OFLOW)
TRAPHANDLER_NOEC(bounds_check_h, T_BOUND)
TRAPHANDLER_NOEC(illegal_op_h, T_ILLOP)
TRAPHANDLER_NOEC(device_not_avail_h, T_DEVICE)
TRAPHANDLER(double_fault_h, T_DBLFLT)
TRAPHANDLER(invalid_tss_h, T_TSS)
TRAPHANDLER(seg_not_present_h, T_SEGNP)
TRAPHANDLER(stack_exc_h, T_STACK)
TRAPHANDLER(general_protection_fault_h, T_GPFLT)
TRAPHANDLER(page_fault_h, T_PGFLT)
TRAPHANDLER_NOEC(fp_error_h, T_FPERR)
TRAPHANDLER_NOEC(alignment_h, T_ALIGN)
TRAPHANDLER_NOEC(machine_check_h, T_MCHK)
TRAPHANDLER_NOEC(simd_h, T_SIMDERR)


/*
 * Lab 3: Your code here for _alltraps
 */
 	_alltraps:
		pushl %ds;
		pushl %es;
		pushal;
		movl $GD_KD, %eax
		movw %eax, %ds;
		movw %eax, %es;
		pushl %esp;
		call trap;
	  


