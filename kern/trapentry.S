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

TRAPHANDLER_NOEC(trap_divide, T_DIVIDE)
TRAPHANDLER_NOEC(trap_debug, T_DEBUG)
TRAPHANDLER_NOEC(trap_nmi, T_NMI)
TRAPHANDLER_NOEC(trap_brkpt, T_BRKPT)
TRAPHANDLER_NOEC(trap_oflow, T_OFLOW)
TRAPHANDLER_NOEC(trap_bound, T_BOUND)
TRAPHANDLER_NOEC(trap_illop, T_ILLOP)
TRAPHANDLER_NOEC(trap_device, T_DEVICE)
TRAPHANDLER(trap_dblflt, T_DBLFLT)
# TRAPHANDLER_NOEC(trap_coproc, T_COPROC) # not used
TRAPHANDLER(trap_tss, T_TSS)
TRAPHANDLER(trap_segnp, T_SEGNP)
TRAPHANDLER(trap_stack, T_STACK)
TRAPHANDLER(trap_gpflt, T_GPFLT)
TRAPHANDLER(trap_pgflt, T_PGFLT)
# TRAPHANDLER_NOEC(trap_res, T_RES) # not used
TRAPHANDLER_NOEC(trap_fperr, T_FPERR)
TRAPHANDLER(trap_align, T_ALIGN)
TRAPHANDLER_NOEC(trap_mchk, T_MCHK)
TRAPHANDLER_NOEC(trap_simderr, T_SIMDERR)

TRAPHANDLER_NOEC(trap_syscall, T_SYSCALL)
TRAPHANDLER_NOEC(trap_default, T_DEFAULT)

/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:
	# make Trapframe
	pushw $0
	pushw %ds
	pushw $0
	pushw %es
	pushal
	# load GD_KD into %ds and %es
	movw $(GD_KD),%ax
	movw %ax,%ds
	movw %ax,%es
	# pointer to the Trapframe as an argument
	pushl %esp
	call trap


