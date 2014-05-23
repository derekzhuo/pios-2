/*
 * Spin locks for multiprocessor mutual exclusion in the kernel.
 *
 * Copyright (C) 1997 Massachusetts Institute of Technology
 * See section "MIT License" in the file LICENSES for licensing terms.
 *
 * Derived from the xv6 instructional operating system from MIT.
 * Adapted for PIOS by Bryan Ford at Yale University.
 */

#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/cpu.h>
#include <kern/spinlock.h>
#include <kern/cons.h>


void
spinlock_init_(struct spinlock *lk, const char *file, int line)
{
	// hong:
	// add by me

	//save the file and line number
	lk->file = file;
	lk->line = line;

	lk->locked = 0;
	lk->cpu = NULL;
	lk->eips[0] = 0;
}

// Acquire the lock.
// Loops (spins) until the lock is acquired.
// Holding a lock for a long time may cause
// other CPUs to waste time spinning to acquire it.
void
spinlock_acquire(struct spinlock *lk)
{
	// hong:
	// add by me

	
	// a1: When acquiring a spinlock, check to make sure the current CPU isn't already holding that spinlock, and panic if it is
	if (spinlock_holding(lk)) {
		panic("spinlock_acquire faild : the spinlock have been held by current cpu\n");
	}

	// The xchg is atomic.
  	// It also serializes, so that reads after acquire are not
  	// reordered before it. 
  	while(xchg(&lk->locked, 1) != 0)
    	;
  
	// a2: Whenever a spinlock is held by a given CPU, save a pointer to that CPU's cpu struct in the spinlock
	lk->cpu = cpu_cur();
	
	// use the debug_trace()  to take a backtrace of the contents of the stack at the time the spinlock was acquired
	debug_trace(read_ebp(),lk->eips);
}

// Release the lock.
void
spinlock_release(struct spinlock *lk)
{
	// hong:
	// add by me

	// b1: when releasing a spinlock, check to make sure that the current CPU is holding the spinlock, and panic if it isn't.
	if (!spinlock_holding(lk)) {
		panic("spinlock_release faild : current cpu didn't hold the spinlock\n");
	}
	
	// b2: clear it to NULL when releasing the spinlock
	lk->cpu = NULL;
	
	lk->eips[0] = 0;

	 // The xchg serializes, so that reads before release are 
  	// not reordered after it.  The 1996 PentiumPro manual (Volume 3,
  	// 7.2) says reads can be carried out speculatively and in
  	// any order, which implies we need to serialize here.
  	// But the 2007 Intel 64 Architecture Memory Ordering White
  	// Paper says that Intel 64 and IA-32 will not move a load
  	// after a store. So lock->locked = 0 would work here.
  	// The xchg being asm volatile ensures gcc emits it after
  	// the above assignments (and after the critical section).
	xchg(&lk->locked, 0);
	
}

// Check whether this cpu is holding the lock.
int
spinlock_holding(spinlock *lock)
{
	// hong:
	// add by me
	return lock->locked && lock->cpu == cpu_cur();
}

// Function that simply recurses to a specified depth.
// The useless return value and volatile parameter are
// so GCC doesn't collapse it via tail-call elimination.
int gcc_noinline
spinlock_godeep(volatile int depth, spinlock* lk)
{
	if (depth==0) { spinlock_acquire(lk); return 1; }
	else return spinlock_godeep(depth-1, lk) * depth;
}

void spinlock_check()
{
	const int NUMLOCKS=10;
	const int NUMRUNS=5;
	int i,j,run;
	const char* file = "spinlock_check";
	spinlock locks[NUMLOCKS];

	// Initialize the locks
	for(i=0;i<NUMLOCKS;i++) spinlock_init_(&locks[i], file, 0);
	// Make sure that all locks have CPU set to NULL initially
	for(i=0;i<NUMLOCKS;i++) assert(locks[i].cpu==NULL);
	// Make sure that all locks have the correct debug info.
	for(i=0;i<NUMLOCKS;i++) assert(locks[i].file==file);

	
	for (run=0;run<NUMRUNS;run++) 
	{
		// Lock all locks
		for(i=0;i<NUMLOCKS;i++)
			spinlock_godeep(i, &locks[i]);
		

		// Make sure that all locks have the right CPU
		for(i=0;i<NUMLOCKS;i++)
			assert(locks[i].cpu == cpu_cur());
		// Make sure that all locks have holding correctly implemented.
		for(i=0;i<NUMLOCKS;i++)
			assert(spinlock_holding(&locks[i]) != 0);
		// Make sure that top i frames are somewhere in godeep.
		for(i=0;i<NUMLOCKS;i++) 
		{
			for(j=0; j<=i && j < DEBUG_TRACEFRAMES ; j++) 
			{
				assert(locks[i].eips[j] >=
					(uint32_t)spinlock_godeep);
				assert(locks[i].eips[j] <
					(uint32_t)spinlock_godeep+100);
			}
		}
		// Release all locks
		for(i=0;i<NUMLOCKS;i++) spinlock_release(&locks[i]);
		// Make sure that the CPU has been cleared
		for(i=0;i<NUMLOCKS;i++) assert(locks[i].cpu == NULL);
		for(i=0;i<NUMLOCKS;i++) assert(locks[i].eips[0]==0);
		// Make sure that all locks have holding correctly implemented.
		
		for(i=0;i<NUMLOCKS;i++) assert(spinlock_holding(&locks[i]) == 0);
	}
	cprintf("spinlock_check() succeeded!\n");
}

