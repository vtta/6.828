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
#include <kern/trap.h>
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
	{ "backtrace", "Display the stack backtrace", mon_backtrace },
	{ "page", "Display page mapping and set or clear flag bits", mon_page },
	{ "mem", "Dump memory contents of giving physical or virtual address", mon_mem },
	{ "stepi", "Single-step debuggee", mon_stepi },
	{ "continue", "Continue executing debuggee", mon_continue }
};

struct pte_bits{
	const char *name;
	int value;
};

struct pte_bits pte_bits_mapping[] = {
	{ "G", PTE_G },
	{ "PS", PTE_PS },
	{ "D", PTE_D },
	{ "A", PTE_A },
	{ "PCD", PTE_PCD },
	{ "PWT", PTE_PWT },
	{ "U", PTE_U },
	{ "W", PTE_W },
	{ "P", PTE_P },
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
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
	cprintf("Stack backtrace:\n");
	uint32_t ebp = read_ebp(), eip;
	struct Eipdebuginfo info;
	while (ebp) {
		eip = *((uint32_t *)ebp+1);
		if (debuginfo_eip(eip, &info) < 0)
			break;
		cprintf("  ebp %08x  eip %08x", ebp, eip );
		if (info.eip_fn_narg)
			cprintf("  args");
		for (int i = 0; i < info.eip_fn_narg; ++i) {
		// for (int i = 0; i < 4; ++i) {
			cprintf(" %08x", *((uint32_t *)ebp+i+2) );
		}
		cprintf("\n    %s:%d: %.*s+%d\n", info.eip_file, info.eip_line,
			info.eip_fn_namelen, info.eip_fn_name, eip - info.eip_fn_addr);
		ebp = *(uint32_t *)ebp;
	}
	return 0;
}

static char *
pg_bits(pte_t pte)
{
    static char buf[32];
    snprintf(buf, 9, "%c%c%c%c%c%c%c%c%c",
            pte & PTE_G ? 'G' : '-',
            pte & PTE_PS ? 'S' : '-',
            pte & PTE_D ? 'D' : '-',
            pte & PTE_A ? 'A' : '-',
            pte & PTE_PCD ? 'C' : '-',
            pte & PTE_PWT ? 'T' : '-',
            pte & PTE_U ? 'U' : '-',
            pte & PTE_W ? 'W' : '-',
            pte & PTE_P ? 'P' : '-');
    return buf;
}

int
mon_page(int argc, char **argv, struct Trapframe *tf)
{
	int const e_invalid_command = 1;
	int const e_invalid_address = 2;
	if (argc < 3) {
		cprintf("Usage:\n");
		cprintf("    %s show begin_address [end_address]\n", argv[0]);
		cprintf("    %s set virtual_address [G] [PS] [D] [A] [PCD] [PWT] [U] [W] [P]\n", argv[0]);
		cprintf("    %s clear virtual_address [G] [PS] [D] [A] [PCD] [PWT] [U] [W] [P]\n", argv[0]);
		return e_invalid_command;
	}
	if (!strcmp(argv[1], "show")) {
		uintptr_t va_begin = ROUNDDOWN(strtol(argv[2], NULL, 16), PGSIZE),
			va_end = argc>3?ROUNDDOWN(strtol(argv[3], NULL, 16), PGSIZE):va_begin;
		if (!(va_begin && va_end)) {
			return e_invalid_address;
		}
		cprintf("VA       Entry    PA       Flags\n");
		pte_t *pte_for_va = NULL;
		for (uintptr_t va = va_begin; va <= va_end; va += 0x1000) {
			pte_for_va = pgdir_walk(kern_pgdir, (const void *)va, false);
			cprintf("%08x %08x ", va, pte_for_va);
			if (pte_for_va)
				cprintf("%08x %s\n", (*pte_for_va) & ~0xFFF , pg_bits(*pte_for_va));
			else
				cprintf("\n");
		}
		return 0;
	} else {
		uintptr_t va = ROUNDDOWN(strtol(argv[2], NULL, 16), PGSIZE);
		if (!va) {
			return e_invalid_address;
		}
		pte_t *pte_for_va = pgdir_walk(kern_pgdir, (const void *)va, false);
		if (!pte_for_va) {
			return e_invalid_address;
		}
		int perm = 0;
		for (int i = 2; i < argc; ++i) {
			for (int j = 0; j < ARRAY_SIZE(pte_bits_mapping); ++j) {
				if (!strcmp(argv[i], pte_bits_mapping[j].name))
					perm |= pte_bits_mapping[j].value;
			}
		}
		if (!strcmp(argv[1], "set")) {
			*pte_for_va |= perm;
		} else if (!strcmp(argv[1], "clear")) {
			*pte_for_va &= ~perm;
		} else {
			return e_invalid_command;
		}
		cprintf("VA       Entry    PA       Flags\n");
		cprintf("%08x %08x %08x %s\n", va, pte_for_va,
			(*pte_for_va) & ~0xFFF, pg_bits(*pte_for_va));
		return 0;
	}
	return e_invalid_command;
}

int
mon_mem(int argc, char **argv, struct Trapframe *tf)
{
	int const e_invalid_command = 1;
	if (argc < 2) {
		cprintf("Usage:\n");
		cprintf("    %s [-v|-p] begin_address [end_address]\n", argv[0]);
		return e_invalid_command;
	}
	bool v = true;
	uintptr_t ba = 0, ea = 0;
	if (argv[1][0] == '-') {
		v = !strcmp(argv[1], "-v");
		ba = strtol(argv[2], NULL, 16);
		ea = argc>3?strtol(argv[3], NULL, 16):ba;
	} else {
		ba = strtol(argv[1], NULL, 16);
		ea = argc>2?strtol(argv[2], NULL, 16):ba;
	}
	if (!v) {
		ba = (uintptr_t)KADDR(ba);
		ea = (uintptr_t)KADDR(ea);
	}
	ba = ROUNDDOWN(ba, 4);
	ea = ROUNDUP(ea, 4);
	cprintf("VA         Data\n");
	for (; ba <= ea; ba+=4) {
		cprintf("[%08x] %08x\n", ba, *(uintptr_t *)ba);
	}
	return 0;
}

int
mon_stepi(int argc, char **argv, struct Trapframe *tf)
{
	int const e_invalid_command = 1;
	if (!tf) {
		cprintf("This command can only be used while debugging\n");
		return e_invalid_command;
	}
	tf->tf_eflags |= FL_TF;
	// exit kernel monitor
	return -1;
}

int
mon_continue(int argc, char **argv, struct Trapframe *tf)
{
	int const e_invalid_command = 1;
	if (!tf) {
		cprintf("This command can only be used while debugging\n");
		return e_invalid_command;
	}
	tf->tf_eflags &= ~FL_TF;
	// exit kernel monitor
	return -1;
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
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
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

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
