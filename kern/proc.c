
/*
 * PIOS process management.
 *
 * Copyright (C) 2010 Yale University.
 * See section "MIT License" in the file LICENSES for licensing terms.
 *
 * Primary author: Bryan Ford
 */

#include <inc/string.h>
#include <inc/syscall.h>

#include <kern/cpu.h>
#include <kern/mem.h>
#include <kern/trap.h>
#include <kern/proc.h>
#include <kern/init.h>


typedef enum log_type {
	LOCK_ACQUIRE	= 0,	
	LOCK_GET,	
	LOCK_RELEASE,	
} log_type;


proc proc_null;		// null process - just leave it initialized to 0

proc *proc_root;	// root process, once it's created in init()

// LAB 2: insert your scheduling data structure declarations here.
struct {
	spinlock lock;
	proc * ready_head;
}proc_ready_que;

void 
proc_log(spinlock lock, log_type type) {
	return ;
	switch(type) {
		case LOCK_ACQUIRE: cprintf("cpu[%d]: acquire lock:%s\n",cpu_cur()->id, lock.file); break;
		case LOCK_GET: cprintf("cpu[%d]: get lock:%s\n",cpu_cur()->id, lock.file);break;
		case LOCK_RELEASE: cprintf("cpu[%d]: release lock:%s\n",cpu_cur()->id, lock.file);break;
		default: panic("wrong proc_log type\n");
	}
}
void
proc_init(void)
{
	static int inited = 0;
	if(inited == 1)
		return ;
 	//if (!cpu_onboot()) {
	//	return ;
	//}
	// your module initialization code here
	// hong: add by me
	inited = 1;
	spinlock_init_(&proc_ready_que.lock, "proc_ready_que",1);
	cprintf("lock name : %s",proc_ready_que.lock.file);
	proc_ready_que.ready_head = NULL;
	
}

// Allocate and initialize a new proc as child 'cn' of parent 'p'.
// Returns NULL if no physical memory available.
proc *
proc_alloc(proc *p, uint32_t cn)
{
	pageinfo *pi = mem_alloc();
	int i;
	if (!pi)
		return NULL;
	mem_incref(pi);

	proc *cp = (proc*)mem_pi2ptr(pi);
	memset(cp, 0, sizeof(proc));
	// hong: add by me 
	
	if(p != NULL) {	
		memmove(cp->id,p->id,strlen(p->id)*sizeof(char));
		i = strlen(p->id);
	} else {
		i = 0;
	}
	cp->id[i] = '[';
	cp->id[i + 1] = cn+ '0';
	cp->id[i + 2] = ']';
	cp->id[i + 3] = '\0';
	//
	
	spinlock_init_(&cp->lock,cp->id,1);
	cp->parent = p;
	cp->state = PROC_STOP;

	// Integer register state
	cp->sv.tf.ds = CPU_GDT_UDATA | 3;
	cp->sv.tf.es = CPU_GDT_UDATA | 3;
	cp->sv.tf.cs = CPU_GDT_UCODE | 3;
	cp->sv.tf.ss = CPU_GDT_UDATA | 3;
	cp->sv.tf.eflags = FL_IF;
	if (p)
		p->child[cn] = cp;
	return cp;
}

// hong >>>>> 
// Put process p in the ready state and add it to the ready queue.
// hong:so that this or some other CPU will eventually run the process at some future invocation of the scheduler. 
void
proc_ready(proc *p)
{
	//cprintf("cpu[%d] in proc_ready\n",cpu_cur()->id);
	// hong:
	// add by me
	assert(p->state != PROC_READY);
    proc_log(p->lock, LOCK_ACQUIRE);
	spinlock_acquire(&(p->lock));
    proc_log(p->lock, LOCK_GET);
	p->state = PROC_READY;
	p->readynext = NULL;
    proc_log(p->lock, LOCK_RELEASE);
	spinlock_release(&(p->lock));
    proc_log( proc_ready_que.lock, LOCK_ACQUIRE);
	spinlock_acquire(&proc_ready_que.lock);
    proc_log( proc_ready_que.lock, LOCK_GET);
	if (proc_ready_que.ready_head == NULL) {
		proc_ready_que.ready_head = p;
	} else {
		proc *cur = proc_ready_que.ready_head;
		while(cur->readynext != NULL) {
			cur = cur->readynext;
		}
		cur->readynext =  p;
	}
    proc_log(proc_ready_que.lock, LOCK_RELEASE);
	spinlock_release(&proc_ready_que.lock);
}

// hong:>>>>
// Save the current process's state before switching to another process.
// Copies trapframe 'tf' into the proc struct,
// and saves any other relevant state such as FPU state.
// The 'entry' parameter is one of:
//	-1	if we entered the kernel via a trap before executing an insn
//	0	if we entered via a syscall and must abort/rollback the syscall
//	1	if we entered via a syscall and are completing the syscall
void
proc_save(proc *p, trapframe *tf, int entry)
{
	//cprintf("cpu[%d] in proc_save\n",cpu_cur()->id);
	// hong: 
	// note : save rather than assign(=)
    proc_log(p->lock, LOCK_ACQUIRE);
	spinlock_acquire(&(p->lock));
    proc_log(p->lock, LOCK_GET);
	switch (entry) {
	case -1 : panic("unknow how to handle proc_save if entry == -1"); break;
	case 0 : tf->eip = (uintptr_t)((char *)tf->eip - 2 );
	case 1 : memmove(&(p->sv.tf), tf, sizeof(trapframe)); break;
	default : panic("unknow entry in proc_save\n");
	}
    proc_log(p->lock, LOCK_RELEASE);
	spinlock_release(&(p->lock));
}

// Go to sleep waiting for a given child process to finish running.
// Parent process 'p' must be running and locked on entry.
// The supplied trapframe represents p's register state on syscall entry.
void gcc_noreturn
proc_wait(proc *p, proc *cp, trapframe *tf)
{

	//cprintf("cpu[%d] in proc_wait\n",cpu_cur()->id);
	// hong: add by me
    proc_log(p->lock, LOCK_ACQUIRE);
	spinlock_acquire(&(p->lock));
    proc_log(p->lock, LOCK_GET);
	p->state = PROC_WAIT;
	p->waitchild = cp;
    proc_log(p->lock, LOCK_RELEASE);
	spinlock_release(&(p->lock));
	// system call blocked ,must rollback
	proc_save(p, tf, 0);
	proc_yield(&(p->sv.tf));
}


// hong >>>>> 
// hong:  Remove some process from the ready queue and run it.
// 	If the ready queue is currently empty,just spin until some other CPU puts some work into the ready queue.
// 	(For good measure, use the pause() function discussed above in the ready loop, although this doesn't affect correctness.) 
void gcc_noreturn
proc_sched(void)
{
	// hong: add by me
	proc * cur;
	for (;;){
	//cprintf("cpu[%d] in proc_sched\n",cpu_cur()->id);
   		 proc_log(proc_ready_que.lock, LOCK_ACQUIRE);
		spinlock_acquire(&proc_ready_que.lock);
    	proc_log(proc_ready_que.lock, LOCK_GET);
		if (proc_ready_que.ready_head != NULL) {
			cur = proc_ready_que.ready_head;
			proc_ready_que.ready_head =  proc_ready_que.ready_head->readynext;
			assert(cur->state == PROC_READY);
			proc_run(cur);
		} else {
			pause();
		}
    	proc_log(proc_ready_que.lock, LOCK_RELEASE);
		spinlock_release(&proc_ready_que.lock);
	}
}


// hong >>>>> 
// Switch to and run a specified process, which must already be locked.
// hong:	Place process p in the PROC_RUN state,mark it as running on this CPU and run it
// This function does not return because it is expected to call trap_return() and enter user space to run the process. 
void gcc_noreturn
proc_run(proc *p)
{
	//cprintf("cpu[%d] in proc_run\n",cpu_cur()->id);
	cpu_cur()->proc = p;
	p->runcpu = cpu_cur();
	p->state = PROC_RUN;
    proc_log(proc_ready_que.lock, LOCK_RELEASE);
	spinlock_release(&proc_ready_que.lock);
	//cprintf("proc_run : eip = 0x%x , cs = 0x%x\n",p->sv.tf.eip,p->sv.tf.esp);
	//cprintf("cpu[%d] run proc%s\n",cpu_cur()->id,p->id);
	trap_return(&p->sv.tf);
}

//// hong >>>>> 
// Yield the current CPU to another ready process.
// Called while handling a timer interrupt.
// hong : 
// 1) Put the currently running process on this CPU back in the PROC_READY state 
// and yield its CPU to some other ready process
// 2)  This function is called with a pointer to a trapframe containing the user state of the process that is yielding the CPU,
// which was typically pushed on the kernel stack on kernel entry. 
// 3) Since the kernel stack is reset on switching to a new process, the old process's state needs to 
// be saved in its proc struct before switching to the new process.
void gcc_noreturn
proc_yield(trapframe *tf)
{
	//cprintf("cpu[%d] in proc_ready\n",cpu_cur()->id);
	proc *p = cpu_cur()->proc;
	proc_save(p, tf, 1);
	proc_ready(p);
	proc_sched();
}

// hong>>>>>
// Put the current process to sleep by "returning" to its parent process.
// Used both when a process calls the SYS_RET system call explicitly,
// and when a process causes an unhandled trap in user mode.
// The 'entry' parameter is as in proc_save().
void gcc_noreturn
proc_ret(trapframe *tf, int entry)
{
	proc *child = cpu_cur()->proc;
	proc *parent = child->parent;
	assert(child->state != PROC_STOP);
    proc_log(child->lock, LOCK_ACQUIRE);
	spinlock_acquire(&(child->lock));
    proc_log(child->lock, LOCK_GET);
	child->state = PROC_STOP;
    proc_log(child->lock, LOCK_RELEASE);
	spinlock_release(&(child->lock));
	proc_save(child, tf, entry);
	if ((parent->state == PROC_WAIT) && (parent->waitchild == child)) {
		proc_ready(parent); //
	}
	proc_sched();
	//panic("proc_ret not implemented");
}

// Helper functions for proc_check()
static void child(int n);
static void grandchild(int n);

static struct procstate child_state;
static char gcc_aligned(16) child_stack[4][PAGESIZE];

static volatile uint32_t pingpong = 0;
static void *recovargs;

void
proc_check(void)
{
	// Spawn 2 child processes, executing on statically allocated stacks.

	int i;
	for (i = 0; i < 4; i++) {
		// Setup register state for child
		uint32_t *esp = (uint32_t*) &child_stack[i][PAGESIZE];
		*--esp = i;	// push argument to child() function
		*--esp = 0;	// fake return address
		child_state.tf.eip = (uint32_t) child;
		child_state.tf.esp = (uint32_t) esp;

		// Use PUT syscall to create each child,
		// but only start the first 2 children for now.
		cprintf("spawning child %d\n", i);
		sys_put(SYS_REGS | (i < 2 ? SYS_START : 0), i, &child_state,
			NULL, NULL, 0);
	}

	// Wait for both children to complete.
	// This should complete without preemptive scheduling
	// when we're running on a 2-processor machine.
	for (i = 0; i < 2; i++) {
		cprintf("waiting for child %d\n", i);
		sys_get(SYS_REGS, i, &child_state, NULL, NULL, 0);
	}
	//cprintf("proc_check() 2-child test succeeded\n");
	cprintf("proc_check() 2-child test succeeded\n");
	// (Re)start all four children, and wait for them.
	// This will require preemptive scheduling to complete
	// if we have less than 4 CPUs.
	cprintf("proc_check: spawning 4 children\n");
	for (i = 0; i < 4; i++) {
		cprintf("spawning child %d\n", i);
		sys_put(SYS_START, i, NULL, NULL, NULL, 0);
	}

	// Wait for all 4 children to complete.
	for (i = 0; i < 4; i++)
		sys_get(0, i, NULL, NULL, NULL, 0);
	cprintf("proc_check() 4-child test succeeded\n");

	// Now do a trap handling test using all 4 children -
	// but they'll _think_ they're all child 0!
	// (We'll lose the register state of the other children.)
	i = 0;
	sys_get(SYS_REGS, i, &child_state, NULL, NULL, 0);
		// get child 0's state
	assert(recovargs == NULL);
	do {
		sys_put(SYS_REGS | SYS_START, i, &child_state, NULL, NULL, 0);
		sys_get(SYS_REGS, i, &child_state, NULL, NULL, 0);
		if (recovargs) {	// trap recovery needed
			trap_check_args *args = recovargs;
			cprintf("recover from trap %d\n",
				child_state.tf.trapno);
			child_state.tf.eip = (uint32_t) args->reip;
			args->trapno = child_state.tf.trapno;
		} else
			assert(child_state.tf.trapno == T_SYSCALL);
		i = (i+1) % 4;	// rotate to next child proc
	} while (child_state.tf.trapno != T_SYSCALL);
	assert(recovargs == NULL);

	cprintf("proc_check() trap reflection test succeeded\n");

	cprintf("proc_check() succeeded!\n");
}

static void child(int n)
{
	// Only first 2 children participate in first pingpong test
	if (n < 2) {
		int i;
		for (i = 0; i < 10; i++) {
			cprintf("in child %d count %d\n", n, i);
			while (pingpong != n) {
				pause();
			}
			xchg(&pingpong, !pingpong);
		}
		sys_ret();
	}

	// Second test, round-robin pingpong between all 4 children
	int i;
	for (i = 0; i < 10; i++) {
		cprintf("in child %d count %d\n", n, i);
		while (pingpong != n)
			pause();
		xchg(&pingpong, (pingpong + 1) % 4);
	}
	sys_ret();

	// Only "child 0" (or the proc that thinks it's child 0), trap check...
	if (n == 0) {
		assert(recovargs == NULL);
		trap_check(&recovargs);
		assert(recovargs == NULL);
		sys_ret();
	}

	panic("child(): shouldn't have gotten here");
}

static void grandchild(int n)
{
	panic("grandchild(): shouldn't have gotten here");
}

