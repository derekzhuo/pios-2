/*
 * CPU setup and management of key protected-mode data structures,
 * such as global descriptor table (GDT) and task state segment (TSS).
 *
 * Copyright (C) 2010 Yale University.
 * See section "MIT License" in the file LICENSES for licensing terms.
 *
 * Primary author: Bryan Ford
 */

#include <inc/assert.h>
#include <inc/string.h>

#include <kern/mem.h>
#include <kern/cpu.h>
#include <kern/init.h>



cpu cpu_boot = {

	// Global descriptor table for bootstrap CPU.
	// The GDTs for other CPUs are copied from this and fixed up.
	//
	// The kernel and user segments are identical except for the DPL.
	// To load the SS register, the CPL must equal the DPL.  Thus,
	// we must duplicate the segments for the user and the kernel.
	//
	// The only descriptor that differs across CPUs is the TSS descriptor.
	//
	gdt: {
		// 0x0 - unused (always faults: for trapping NULL far pointers)
		[0] = SEGDESC_NULL,

		// 0x08 - kernel code segment
		// hong:
		// why the first arg in SEGDESC32(app) is 1????? ????????????????
		[CPU_GDT_KCODE >> 3] = SEGDESC32(1, STA_X | STA_R, 0x0,
					0xffffffff, 0),

		// 0x10 - kernel data segment
		[CPU_GDT_KDATA >> 3] = SEGDESC32(1, STA_W, 0x0,
					0xffffffff, 0),

		// hong: 
		// add by me
		[CPU_GDT_UCODE >> 3] = SEGDESC32(1, STA_X|STA_R,0x0,
					0xffffffff, 3 ),
		
		[CPU_GDT_UDATA >> 3] = SEGDESC32(1, STA_W, 0x0, 
					0xffffffff, 3),

		[CPU_GDT_UDTLS >> 3] = SEGDESC32(1, STA_W, 0x0, 
					0xffffffff, 3),

		 //c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
 		 //c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
	},

	magic: CPU_MAGIC
};


void cpu_init()
{
	cpu *c = cpu_cur();

	// Load the GDT
	struct pseudodesc gdt_pd = {
		sizeof(c->gdt) - 1, (uint32_t) c->gdt };
	asm volatile("lgdt %0" : : "m" (gdt_pd));

	// Reload all segment registers.
	asm volatile("movw %%ax,%%gs" :: "a" (CPU_GDT_UDATA|3));
	asm volatile("movw %%ax,%%fs" :: "a" (CPU_GDT_UDATA|3));
	asm volatile("movw %%ax,%%es" :: "a" (CPU_GDT_KDATA));
	asm volatile("movw %%ax,%%ds" :: "a" (CPU_GDT_KDATA));
	asm volatile("movw %%ax,%%ss" :: "a" (CPU_GDT_KDATA));
	asm volatile("ljmp %0,$1f\n 1:\n" :: "i" (CPU_GDT_KCODE)); // reload CS

	// We don't need an LDT.
	asm volatile("lldt %%ax" :: "a" (0));

	// hong:
	// add by me
	//TODO : init TSS

	c->tss.ts_ss0 = CPU_GDT_KDATA;
	c->tss.ts_esp0 = (uintptr_t)(c->kstackhi);
	c->gdt[CPU_GDT_TSS >> 3] = SEGDESC16(0,STS_T32A,(uintptr_t)(&c->tss),sizeof(c->tss)-1,0);
	ltr(CPU_GDT_TSS);
	
}


