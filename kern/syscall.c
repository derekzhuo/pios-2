/*
 * System call handling.
 *
 * Copyright (C) 1997 Massachusetts Institute of Technology
 * See section "MIT License" in the file LICENSES for licensing terms.
 *
 * Derived from the xv6 instructional operating system from MIT.
 * Adapted for PIOS by Bryan Ford at Yale University.
 */

#include <inc/x86.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/trap.h>
#include <inc/syscall.h>

#include <kern/cpu.h>
#include <kern/trap.h>
#include <kern/proc.h>
#include <kern/syscall.h>





// This bit mask defines the eflags bits user code is allowed to set.
#define FL_USER		(FL_CF|FL_PF|FL_AF|FL_ZF|FL_SF|FL_DF|FL_OF)


// During a system call, generate a specific processor trap -
// as if the user code's INT 0x30 instruction had caused it -
// and reflect the trap to the parent process as with other traps.
static void gcc_noreturn
systrap(trapframe *utf, int trapno, int err)
{
	proc *parent = proc_cur();
	int i;
	for (i = 0; i < PROC_CHILDREN; i++) {
		if (!parent->child[i] || !spinlock_holding(&(parent->child[i]->lock))) {
			continue;
		}
		spinlock_release(&(parent->child[i]->lock));
	}
	if (spinlock_holding(&(parent->lock))) {
		spinlock_release(&(parent->lock));
	}
	
	assert(utf->trapno == T_SYSCALL);
	utf->trapno = trapno;
	utf->err = err;
	// TODO: how to set the entry , mybe 0 0r 1 all work,  depende the parent process
	proc_ret(utf, 1);
}

// Recover from a trap that occurs during a copyin or copyout,
// by aborting the system call and reflecting the trap to the parent process,
// behaving as if the user program's INT instruction had caused the trap.
// This uses the 'recover' pointer in the current cpu struct,
// and invokes systrap() above to blame the trap on the user process.
//
// Notes:
// - Be sure the parent gets the correct trapno, err, and eip values.
// - Be sure to release any spinlocks you were holding during the copyin/out.
//
static void gcc_noreturn
sysrecover(trapframe *ktf, void *recoverdata)
{
	cpu_cur()->recover = NULL;
	systrap((trapframe *)recoverdata, ktf->trapno, ktf->err);
}

// Check a user virtual address block for validity:
// i.e., make sure the complete area specified lies in
// the user address space between VM_USERLO and VM_USERHI.
// If not, abort the syscall by sending a T_PGFLT to the parent,
// again as if the user program's INT instruction was to blame.
//
// Note: Be careful that your arithmetic works correctly
// even if size is very large, e.g., if uva+size wraps around!
//
static void checkva(trapframe *utf, uint32_t uva, size_t size)
{
	// TODO: implement "if" statement and err arg in systrap()
	if ((size < 0) ||
		(uva < VM_USERLO) || (uva >= VM_USERHI) ||
		((VM_USERHI - uva)< size)) {
		systrap(utf, T_PGFLT, 0);
	}
}

// Copy data to/from user space,
// using checkva() above to validate the address range
// and using sysrecover() to recover from any traps during the copy.
void usercopy(trapframe *utf, bool copyout,
			void *kva, uint32_t uva, size_t size)
{
	checkva(utf, uva, size);

	// Now do the copy, but recover from page faults.
	cpu_cur()->recover = sysrecover;
	cpu_cur()->recoverdata = (void *)utf;
	if (copyout) {
		memmove((void *)uva, kva, size);
	} else {
		memmove(kva, (void *)uva, size);
	}
	cpu_cur()->recover = NULL;
}

static void
do_cputs(trapframe *tf, uint32_t cmd)
{
	//cprintf("in do_cputs , tf->regs.ebx 0x%x\n",tf->regs.ebx);
	// Print the string supplied by the user: pointer in EBX
	pageinfo *pi =mem_alloc();
	char *str = (char *)mem_pi2ptr(pi);
	uint32_t len = strlen((char *)tf->regs.ebx) + 1;
	assert(pi != NULL);
	assert(len > 0 && len <= PAGESIZE);
	usercopy(tf, false, (void *)str, tf->regs.ebx, len);
	
	cprintf("%s", str);
	mem_free(pi);

	trap_return(tf);	// syscall completed
}
// hong:  sys_put(uint32_t flags, uint8_t child, cpustate *cpu):
// Initialize one of the calling process's 256 child process slots, as indicated by child, 
// and optionally start the child process running concurrently with the parent. 
// The flags argument :
//     1) SYS_REGS :
//		calling process wishes to set the register state of the child proces from cpu->tf
//		otherwise the child's state remains unmodified (or initialized to all 0's, if the child is newly created).
// 	    >>The kernel shouldn't allow user processes to set all register state in another process: in particular, 
// 		the kernel should force all the segment registers in the child's trapframe to the usual user-mode code and data segments, 
//		and should only allow "non-sensitive" bits in EFLAGS, such as the arithmetic condition code bits,
//		to be set via this system call.
//     2) SYS_START : 
//		indicates that that the child process should start executing; 
// 	       otherwise the child remains in the stopped state
//  This system call require :
//  	  3) specified child to be in the PROC_STOP state
//		 if not : 
// 			the kernel pts the parent process to sleep waiting for the child to stop
//		(other words: parent goes into PROC_WAIT sits there until the child enters the PROC_STOP ,at which point
//			the parent wakes up and restarts its PUT system call as described above)
static void
do_put(trapframe* tf){
	pageinfo *pi = mem_alloc();
	procstate *cpustate = (procstate *)mem_pi2ptr(pi);
	uint32_t cn = (uint32_t)tf->regs.edx;
	proc *parent = cpu_cur()->proc;
	proc *child =  parent->child[cn];
	assert(pi != NULL);
	assert(cn >= 0 && cn < PROC_CHILDREN);
	assert(parent != NULL);
	// at each moment the specific parent process could only be excute by only on cpu 
	// so the specific child process's proc_alloc didn't need a lock
	//cprintf("in do_put : parent(0x%x)\n",parent);
	if (child == NULL) {
		child = proc_alloc(parent, cn);
		assert(child != NULL);
		//cprintf("in do_put : new child(0x%x)\n",child);
	}
	// get the child's lock the ensure that when step into the proc_wait, the child's state didn't be modified
	spinlock_acquire(&child->lock);
	if (child->state != PROC_STOP) {
		proc_wait(parent, child, tf);
	}
	spinlock_release(&child->lock);
	if (tf->regs.eax & SYS_REGS) {
		spinlock_acquire(&(child->lock));
		assert(pi != NULL);
		usercopy(tf, false, (void *)cpustate, tf->regs.ebx, sizeof(procstate));
		memmove(&(child->sv.tf.regs), &(cpustate->tf.regs), sizeof(pushregs));
		child->sv.tf.ds = CPU_GDT_UDATA | 3;
		child->sv.tf.es = CPU_GDT_UDATA | 3;
		child->sv.tf.cs = CPU_GDT_UCODE | 3;
		child->sv.tf.ss = CPU_GDT_UDATA | 3;
		child->sv.tf.eip = cpustate->tf.eip;
		child->sv.tf.esp = cpustate->tf.esp;
		child->sv.tf.eflags &= FL_USER;
		child->sv.tf.fs = cpustate->tf.fs;
		child->sv.tf.gs = cpustate->tf.gs;
		// TODO: how to put eflags
		spinlock_release(&(child->lock));
	}
	if (tf->regs.eax & SYS_COPY) {
		uint32_t size = tf->regs.ecx;
		uint32_t sva = tf->regs.esi;
		uint32_t dva = tf->regs.edi;
		pmap_copy(parent->pdir, sva, child->pdir, dva, size);
	}
	if (tf->regs.eax & SYS_ZERO) {
		panic("unimplement\n");
	}
	if (tf->regs.eax & SYS_START) {
		assert(child->state == PROC_STOP);
		proc_ready(child);
	}
	mem_free(pi);
	trap_return(tf);
}


// hong: sys_get(uint32_t flags, uint8_t child, cpustate *cpu): 
// Collect information from a child process. 
// first waits for the child to stop if the child isn't already in the stopped state. 
// The flags argument : what information the parent retrieves from the child.
// 	SYS_REGS: 
//		retrieve the register state of the child process; kernel copies the child's reg state into the cpu->tf
static void
do_get(trapframe* tf) {
	procstate *cpustate  = (procstate *)tf->regs.ebx;
	uint32_t  cn = (uint32_t)tf->regs.edx;
	proc *parent = cpu_cur()->proc;
	proc *child = parent->child[cn];
	assert(parent != NULL);
	assert(child != NULL);
	spinlock_acquire(&child->lock);
	if (child->state != PROC_STOP) {
		proc_wait(parent, child, tf);
	}
	spinlock_release(&child->lock);
	if (tf->regs.eax & SYS_REGS) {
		usercopy(tf, true, (void *)(&child->sv.tf), mem_phys(&cpustate->tf), sizeof(trapframe));
		//memmove(&(cpustate->tf), &(child->sv.tf),sizeof(trapframe));
	}
	if (tf->regs.eax & SYS_COPY) {
		uint32_t size = tf->regs.ecx;
		uint32_t sva = tf->regs.esi;
		uint32_t dva = tf->regs.edi;
		pmap_copy(child->pdir , sva, parent->pdir, dva, size);
	}
	if (tf->regs.eax & SYS_ZERO) {
		panic("unimplement\n");
	}
	if (tf->regs.eax & SYS_MERGE) {
		panic("unimplement\n");
	}
	trap_return(tf);
}


// hong: sys_ret(void):
// Explicitly "returns" from a child process to its parent.
// The child process goes into the PROC_STOP state, and wakes up its parent if the parent happened to be
// waiting for this particular child, as a result of either a sys_put() or sys_get().
static void
do_ret(trapframe *tf) {
	proc_ret(tf, 1);
}


// Common function to handle all system calls -
// decode the system call type and call an appropriate handler function.
// Be sure to handle undefined system calls appropriately.
// TODO:  modify the system call handlers to use usercopy when copying system call argument data into or out of user space. 
void
syscall(trapframe *tf)
{
	// EAX register holds system call command/flags
	uint32_t cmd = tf->regs.eax;
	//cprintf("cpu[%d] in syscall()\n", cpu_cur()->id);
	switch (cmd & SYS_TYPE) {
	case SYS_CPUTS:	return do_cputs(tf, cmd);
	// Your implementations of SYS_PUT, SYS_GET, SYS_RET here...
	case SYS_PUT: do_put(tf); break;
	case SYS_GET: do_get(tf); break;
	case SYS_RET: return do_ret(tf);
	default: panic("unhandle syscall\n"); return;		// handle as a regular trap
	}
}

