/*
 * Kernel initialization.
 *
 * Copyright (C) 1997 Massachusetts Institute of Technology
 * See section "MIT License" in the file LICENSES for licensing terms.
 *
 * Derived from the MIT Exokernel and JOS.
 * Adapted for PIOS by Bryan Ford at Yale University.
 */

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/cdefs.h>
#include <inc/elf.h>
#include <inc/vm.h>

#include <kern/init.h>
#include <kern/cons.h>
#include <kern/debug.h>
#include <kern/mem.h>
#include <kern/cpu.h>
#include <kern/trap.h>
#include <kern/spinlock.h>
#include <kern/mp.h>
#include <kern/proc.h>

#include <dev/pic.h>
#include <dev/lapic.h>
#include <dev/ioapic.h>


// User-mode stack for user(), below, to run on.
static char gcc_aligned(16) user_stack[PAGESIZE];

// Lab 3: ELF executable containing root process, linked into the kernel
#ifndef ROOTEXE_START
#define ROOTEXE_START _binary_obj_user_testvm_start
#endif
extern char ROOTEXE_START[];
void read_elf(char *, pde_t *);

// Called first from entry.S on the bootstrap processor,
// and later from boot/bootother.S on all other processors.
// As a rule, "init" functions in PIOS are called once on EACH processor.
void
init(void)
{
	// hong:
	// can not find start  --> in entry.S
	// edata, end, --> 
	extern char start[], edata[], end[];
	
	// Before anything else, complete the ELF loading process.
	// Clear all uninitialized global data (BSS) in our program
	// ensuring that all static/global variables start out zero.
	if (cpu_onboot())
		memset(edata, 0, end - edata);

	// Initialize the console.
	// Can't call cprintf until after we do this!
	// hong :
	// the first thing the kernel does is initialize the console device driver so that your kernel can produce visible output. 
	cons_init();
	// Lab 1: test cprintf and debug_trace
	cprintf("1234 decimal is %o octal!\n", 1234);
	debug_check();	
	// Initialize and load the bootstrap CPU's GDT, TSS, and IDT.
	cpu_init();
	trap_init();
	// Physical memory detection/initialization.
	// Can't call mem_alloc until after we do this!
	mem_init();
	
	// Lab 2: check spinlock implementation
	if (cpu_onboot())
		spinlock_check();

	// Initialize the paged virtual memory system.
	pmap_init();
	// Find and start other procqessors in a multiprocessor system
	mp_init();		// Find info about processors in system
	pic_init();		// setup the legacy PIC (mainly to disable it)
	ioapic_init();		// prepare to handle external device interrupts
	lapic_init();		// setup this CPU's local APIC
	proc_init();
	cpu_bootothers();	// Get other processors started
	cprintf("CPU %d (%s) has booted\n", cpu_cur()->id,
		cpu_onboot() ? "BP" : "AP");
	
	// Lab 1: change this so it enters user() in user mode,
	// running on the user_stack declared above,
	// instead of just calling user() directly.
	/*
	if (!cpu_onboot())
			while(1);
			
	 trapframe tf = {
		gs: CPU_GDT_UDATA | 3,
		fs: CPU_GDT_UDATA | 3,
		es: CPU_GDT_UDATA | 3,
		ds: CPU_GDT_UDATA | 3,
		cs: CPU_GDT_UCODE | 3,
		ss: CPU_GDT_UDATA | 3,
		eflags: FL_IOPL_3,
		eip: (uint32_t)user,
		esp: (uint32_t)&user_stack[PAGESIZE],
	};
	 	cprintf ("to user\n");
	trap_return(&tf);*/
	/*
	proc *user_proc;
	if(cpu_onboot()) {
		user_proc = proc_alloc(NULL,0);
		user_proc->sv.tf.esp = (uint32_t)&user_stack[PAGESIZE];
		user_proc->sv.tf.eip =  (uint32_t)user;
		user_proc->sv.tf.eflags = FL_IF;
		user_proc->sv.tf.gs = CPU_GDT_UDATA | 3;
		user_proc->sv.tf.fs = CPU_GDT_UDATA | 3;
		proc_ready(user_proc);
	}*/
	
	proc *user_proc;
	if(cpu_onboot()) {
		user_proc = proc_alloc(NULL,0);
		read_elf(ROOTEXE_START, user_proc->pdir);
		user_proc->sv.tf.esp = (uint32_t)(VM_USERHI -1);
		memset(mem_ptr(VM_USERHI - PAGESIZE), 0, PAGESIZE);
		user_proc->sv.tf.eip =  (uint32_t)(0x40000100);
		user_proc->sv.tf.eflags = FL_IF;
		//user_proc->sv.tf.eflags = FL_IOPL_3;
		user_proc->sv.tf.gs = CPU_GDT_UDATA | 3;
		user_proc->sv.tf.fs = CPU_GDT_UDATA | 3;
		proc_ready(user_proc);
	}
	proc_sched();
	user();
}

// This is the first function that gets run in user mode (ring 3).
// It acts as PIOS's "root process",
// of which all other processes are descendants.
void
user()
{
	// hong: system haven't complete 
	 cprintf("in user()\n");
	assert(read_esp() > (uint32_t) &user_stack[0]);
	// hong:
	// sizeof(user_stack) == 4096
	assert(read_esp() < (uint32_t) &user_stack[sizeof(user_stack)]);

	// Check the system call and process scheduling code.
	proc_check();

	done();
}

// This is a function that we call when the kernel is "done" -
// it just puts the processor into an infinite loop.
// We make this a function so that we can set a breakpoints on it.
// Our grade scripts use this breakpoint to know when to stop QEMU.
void gcc_noreturn
done()
{
	cprintf("in done\n");
	while (1)
		;	// just spin
}

void read_elf(char * elf_start, pde_t * pdir) {
	elfhdr *elf = (elfhdr *)elf_start;
	sechdr *sh;
	pte_t *pte;
	uint32_t va, va_start, va_end;
	uint16_t i;
	uint32_t j;
	char * load_start;
	lcr3(mem_phys(pdir));
	//cprintf("in read_elf , pmap_bootpdir 0x%x\n",pmap_bootpdir);
	// First allocate and map all the pages a program segment covers, 
	sh = (sechdr *)(elf_start + elf->e_shoff);
	//cprintf("number of section : %d\n",elf->e_shnum);
	
	for (i = 0; i < elf->e_shnum; i++) {
		sh ++;
		if ((sh->sh_type != ELF_SHT_PROGBITS) &&
			(sh->sh_type != ELF_SHT_NOBITS)) {
			continue;
		}
		if (sh->sh_addr == 0x0) {
			continue;
		}
		//cprintf("name(%d) : 0x%x\n",i,sh->sh_name);
		va_start  = sh->sh_addr & ~0xFFF;
		va_end = (sh->sh_addr + sh->sh_size - 1) & ~0xFFF;
		//cprintf("va_start(0x%x) : 0x%x\nva_end(0x%x) : 0x%x\n>>>sh_off : 0x%x\nsh_size : 0x%x\n\n\n",sh->sh_addr, va_start, sh->sh_addr + sh->sh_size -1,va_end,sh->sh_offset,sh->sh_size);

		// use pmap_insert to alloc page , it may remove some page but it doesn't matter,
		// because the page doesn't have any content
		// after pmap_insert , the [sh->sh_addr, sh->sh_addr + sh->sh_size) have the specific phy addr.
		for (va = va_start; va <= va_end; va += PAGESIZE) {
			//cprintf("va : 0x%x\n",va);
			if (pmap_insert(pdir, mem_alloc(), va, PTE_U | PTE_W | PTE_P) == NULL) {
				panic("in read_elf\n");
			}
		}
	}
	// then initialize the segment all at once by accessing it at its virtual address. 
	sh = (sechdr *)(elf_start + elf->e_shoff);
	for (i = 0 ; i < elf->e_shnum; i++) {
		sh ++;
		if ((sh->sh_type != ELF_SHT_PROGBITS) &&
			(sh->sh_type != ELF_SHT_NOBITS)) {
			continue;
		}
		if (sh->sh_addr == 0x0) {
			continue;
		}
		va_start = sh->sh_addr;
		load_start = elf_start + sh->sh_offset;
		for (j =  0; j < sh->sh_size; j++) {
			if (sh->sh_type == ELF_SHT_PROGBITS) {
				*((char *)(va_start) + j) =  *((load_start) + j);
			} else {
				*((char *)(va_start) + j) =  0;
			}
		}
	}
	
	// make some PTE to read-only
	sh = (sechdr *)(elf_start + elf->e_shoff);
	for (i = 0 ; i < elf->e_shnum; i++) {
		sh ++;
		if ((sh->sh_type != ELF_SHT_PROGBITS) &&
			(sh->sh_type != ELF_SHT_NOBITS)) {
			continue;
		}
		if (sh->sh_addr == 0x0) {
			continue;
		}
		// SHF_WRITE
		if (sh->sh_flags & 0x1) {
			continue;
		}
		va_start  = sh->sh_addr & ~0xFFF;
		va_end = (sh->sh_addr + sh->sh_size - 1) & ~0xFFF;
		for (va = va_start; va <= va_end; va += PAGESIZE) {
			//cprintf("va : 0x%x\n",va);
			pte =  pmap_walk(pdir, va, true);
			assert(pte != NULL);
			assert(*pte != PTE_ZERO);
			*pte = (*pte) & (~PTE_W);
			assert(!(*pte & PTE_W));
		}
	}

	// alloc stack for the testvm
	if (pmap_insert(pdir, mem_alloc(), VM_USERHI - PAGESIZE, PTE_U | PTE_W | PTE_P) == NULL) {
				panic("in read_elf\n");
	}

	// check the permission for each section
	/*uint32_t check_va = 0x40000100;
	for(i = 0; i < 0x3a75; i++)
		assert ( !(*pmap_walk(pdir ,check_va + i,false) & PTE_W) );
	check_va = 0x40003b80;
	for(i = 0; i < 0x000abc; i++)
		assert ( !(*pmap_walk(pdir ,check_va + i,false) & PTE_W) );
	check_va = 0x4000463c;
	for(i = 0; i < 0x000058; i++)
		assert ( !(*pmap_walk(pdir ,check_va + i,false) & PTE_W) );
	check_va = 0x400056a0;
	for(i = 0; i < 0x000600; i++)
		assert ( (*pmap_walk(pdir ,check_va + i,false) & PTE_W) );
	check_va = 0x40005ca0;
	for(i = 0; i < 0x002128; i++)
		assert ( (*pmap_walk(pdir ,check_va + i,false) & PTE_W) );*/
	
	lcr3(mem_phys(pdir));
	//uint32_t * kkk = (uint32_t *)0x40003b80;
	//*kkk = 3333;
	//cprintf("kkk 0x%x\n",*kkk);
	cprintf("read_elf complete\n");
}
//add-symbol-file obj/user/testvm 0x40000100