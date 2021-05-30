/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


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
#define TRAPHANDLER(name, num, dpl)                             \
    .text;                                                      \
    .globl name;    /* define global symbol for 'name' */       \
	.type name, @function;  /* symbol type is function */       \
    .align 2;       /* align function definition */	            \
    name:           /* function starts here */                  \
    pushl $(num);                                               \
    jmp _alltraps;                                              \
    .data;                                                      \
    .long name;                                                 \
    .byte num;                                                  \
    /* interrupts are always masked inside the kernel           \
     * and are always handled by the kernel                     \
     */                                                         \
    .byte 0;                                                    \
    .byte GD_KT;                                                \
    .byte dpl

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num, dpl)                        \
    .text;                                                      \
    .globl name;                                                \
    .type name, @function;                                      \
    .align 2;                                                   \
    name:                                                       \
    pushl $0;                                                   \
    pushl $(num);                                               \
	jmp _alltraps;                                              \
    .data;                                                      \
    .long name;                                                 \
    .byte num;                                                  \
    .byte 0;                                                    \
    .byte GD_KT;                                                \
    .byte dpl

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */

.data
.global interrupt_info
.global interrupt_info_end
interrupt_info:
TRAPHANDLER_NOEC(   div_error_h,                T_DIVIDE,      0)
TRAPHANDLER_NOEC(   debug_h,                    T_DEBUG,       0)
TRAPHANDLER_NOEC(   nmi_h,                      T_NMI,         0)
TRAPHANDLER_NOEC(   breakpoint_h,               T_BRKPT,       3)
TRAPHANDLER_NOEC(   overflow_h,                 T_OFLOW,       0)
TRAPHANDLER_NOEC(   bounds_check_h,             T_BOUND,       0)
TRAPHANDLER_NOEC(   illegal_op_h,               T_ILLOP,       0)
TRAPHANDLER_NOEC(   device_not_avail_h,         T_DEVICE,      0)
TRAPHANDLER(        double_fault_h,             T_DBLFLT,      0)
TRAPHANDLER(        invalid_tss_h,              T_TSS,         0)
TRAPHANDLER(        seg_not_present_h,          T_SEGNP,       0)
TRAPHANDLER(        stack_exc_h,                T_STACK,       0)
TRAPHANDLER(        general_protection_fault_h, T_GPFLT,       0)
TRAPHANDLER(        page_fault_h,               T_PGFLT,       0)
TRAPHANDLER_NOEC(   fp_error_h,                 T_FPERR,       0)
TRAPHANDLER_NOEC(   alignment_h,                T_ALIGN,       0)
TRAPHANDLER_NOEC(   machine_check_h,            T_MCHK,        0)
TRAPHANDLER_NOEC(   simd_h,                     T_SIMDERR,     0)
TRAPHANDLER_NOEC(   syscall_h,                  T_SYSCALL,     3)
TRAPHANDLER_NOEC(   irq0_h,                     IRQ_OFFSET+0,  0)
TRAPHANDLER_NOEC(   irq1_h,                     IRQ_OFFSET+1,  0)
TRAPHANDLER_NOEC(   irq2_h,                     IRQ_OFFSET+2,  0)
TRAPHANDLER_NOEC(   irq3_h,                     IRQ_OFFSET+3,  0)
TRAPHANDLER_NOEC(   irq4_h,                     IRQ_OFFSET+4,  0)
TRAPHANDLER_NOEC(   irq5_h,                     IRQ_OFFSET+5,  0)
TRAPHANDLER_NOEC(   irq6_h,                     IRQ_OFFSET+6,  0)
TRAPHANDLER_NOEC(   irq7_h,                     IRQ_OFFSET+7,  0)
TRAPHANDLER_NOEC(   irq8_h,                     IRQ_OFFSET+8,  0)
TRAPHANDLER_NOEC(   irq9_h,                     IRQ_OFFSET+9,  0)
TRAPHANDLER_NOEC(   irq10_h,                    IRQ_OFFSET+10, 0)
TRAPHANDLER_NOEC(   irq11_h,                    IRQ_OFFSET+11, 0)
TRAPHANDLER_NOEC(   irq12_h,                    IRQ_OFFSET+12, 0)
TRAPHANDLER_NOEC(   irq13_h,                    IRQ_OFFSET+13, 0)
TRAPHANDLER_NOEC(   irq14_h,                    IRQ_OFFSET+14, 0)
interrupt_info_end: .long interrupt_info_end


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
	  


