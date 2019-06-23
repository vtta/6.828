// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

void
dump_user_stack(void)
{
	for (uintptr_t *i = (uintptr_t *)USTACKTOP;
		i != (uintptr_t *)(USTACKTOP - PGSIZE); i -= 4)
		cprintf("[%08x] %08x %08x %08x %08x\n",
			i - 4, *(i - 4), *(i - 3), *(i - 2), *(i - 1));
}

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

	// cprintf("[%08x] utf eip: %x\n", utf->utf_eip);

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	// extern volatile pde_t uvpd[];
	// extern volatile pte_t uvpt[];
	cprintf("env %08x  err %01x  va %08x  pde %08x  pte %08x\n",
		sys_getenvid(), err, addr, uvpd[PDX(addr)], uvpt[PGNUM(addr)]);
	if (!(err & FEC_WR && uvpt[PGNUM(addr)] & PTE_COW)) {
		cprintf("[%08x] user fault va %08x ip %08x\n",
			sys_getenvid(), addr, utf->utf_eip);
		panic("not a write to copy-on-write page");
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	addr = ROUNDDOWN(addr, PGSIZE);
	if ((r = sys_page_alloc(0, PFTEMP, PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);
	memmove((void *)PFTEMP, addr, PGSIZE);
	if ((r = sys_page_map(0, PFTEMP, 0, addr, PTE_W)) < 0)
		panic("sys_page_map: %e", r);
	if ((r = sys_page_unmap(0, PFTEMP)) < 0)
		panic("sys_page_unmap: %e", r);

	// panic("pgfault not implemented");
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
	// panic("duppage not implemented");
	// map COW into child address space
	void *va = (void *)(pn * PGSIZE);
	if ((r = sys_page_map(0, va, envid, va, PTE_COW)) < 0)
		return r;
	// map COW into own address space
	if ((r = sys_page_map(0, va, 0, va, PTE_COW)) < 0)
		return r;

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
	// dump_user_stack();

	// LAB 4: Your code here.
	// panic("fork not implemented");
	int r;
	extern void _pgfault_upcall(void);
	set_pgfault_handler(pgfault);

	envid_t envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0) {
		// We're the child.
		// The copied value of the global variable 'thisenv'
		// is no longer valid (it refers to the parent!).
		// Fix it and return 0.
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// We're the parent.

	// extern volatile pde_t uvpd[];
	// extern volatile pte_t uvpt[];
	// UXSTACKTOP == UTOP
	for (unsigned pn = 0; pn < PGNUM(UXSTACKTOP - PGSIZE); ++pn) {
		if (!(uvpd[pn >> (PDXSHIFT - PTXSHIFT)] & PTE_P))
			continue;
		if (!(uvpt[pn] & PTE_W || uvpt[pn] & PTE_COW))
			continue;
		if ((r = duppage(envid, pn)) < 0)
			panic("duppage: %e", r);
	}

	// deal with user exception stack
	// first page fault (push to cow stack) in parent will happen here
	if ((r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE),
		PTE_W)) < 0)
		panic("sys_page_alloc: %e", r);

	// set up the user page fault entrypoint for the child
	if ((r = sys_env_set_pgfault_upcall(envid, _pgfault_upcall)) < 0)
		panic("sys_env_set_pgfault_upcall: %e", r);
	// _pgfault_handler is in the address space before fork

	// Start the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);

	// dump_user_stack();

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
