/*
 * Copyright (C) 2020 user94729 (https://omegazero.org/) and contributors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Covered Software is provided under this License on an "as is" basis, without warranty of any kind,
 * either expressed, implied, or statutory, including, without limitation, warranties that the Covered Software
 * is free of defects, merchantable, fit for a particular purpose or non-infringing.
 * The entire risk as to the quality and performance of the Covered Software is with You.
 */

#include <klibc/stdlib.h>
#include <klibc/stdint.h>
#include <klibc/stdbool.h>
#include <klibc/stdarg.h>
#include <klibc/string.h>
#include <klibc/stdio.h>
#include <kernel/kb.h>
#include <kernel/serial.h>
#include <arch/idt.h>
#include <arch/gdt.h>
#include <x86/kb.h>
#include <x86/serial.h>
#include <x86/irq.h>
#include <x86/pic.h>
#include <x86/rtc.h>
#include <x86/x86.h>
#include <arch_gb.h>


void x86_on_irq0(idt_interrupt_frame* frame);


static void (*timerHandler)() = NULL;


status_t arch_platform_initialize(){
	status_t status = 0;

	status = gdt_init();
	CERROR();
	status = idt_init();
	CERROR();
	status = irq_init();
	CERROR();

	reloc_ptr((void**) &timerHandler);

	status = pic_init(X86_HW_INT_BASE, X86_HW_INT_BASE + 8);
	CERROR();
	pic_set_default_isrs(X86_HW_INT_BASE, X86_HW_INT_BASE + 8);
	idt_set_isr(X86_HW_INT_BASE, 0x8, IDT_FLAGS_PRESENT | IDT_FLAGS_TYPE_INT_32, &x86_on_irq0);
	idt_set_isr(X86_HW_INT_BASE + 1, 0x8, IDT_FLAGS_PRESENT | IDT_FLAGS_TYPE_INT_32, &kb_int);
	idt_set_isr(X86_HW_INT_BASE + 3, 0x8, IDT_FLAGS_PRESENT | IDT_FLAGS_TYPE_INT_32, &serial_int);
	idt_set_isr(X86_HW_INT_BASE + 4, 0x8, IDT_FLAGS_PRESENT | IDT_FLAGS_TYPE_INT_32, &serial_int);
	pic_enable_interrupts();

	_end:
	return status;
}

status_t arch_platform_reset(){
	status_t status = 0;
	// IDT will be reset by s3boot
	pit_reset();
	pic_disable_interrupts();
	pic_reset();
	return status;
}


status_t arch_set_timer(size_t frequency){
	pit_init(frequency);
	return TSX_SUCCESS;
}

void arch_on_timer_fire(void (*handler)()){
	timerHandler = handler;
}


void arch_sleep(size_t ms){
	pit_sleep(ms);
}

uint16_t arch_sleep_kb(size_t ms){
	return pit_sleep_kb(ms);
}

size_t arch_time(){
	return pit_get_time();
}


void arch_enable_hw_interrupts(){
	pic_enable_interrupts();
}

void arch_disable_hw_interrupts(){
	pic_disable_interrupts();
}

bool arch_is_hw_interrupts_enabled(){
	return pic_is_interrupts_enabled();
}

bool arch_is_hw_interrupt_running(){
	return pic_is_interrupt_running();
}


void arch_relocation(size_t oldAddr, size_t newAddr){
	gdt_relocation(oldAddr, newAddr);
	idt_relocation(oldAddr, newAddr);
}


size_t arch_rand(size_t max){
	return kernel_pseudorandom(max);
}


uint64_t arch_real_time(){
	uint16_t year;
	uint8_t month, day, hour, minute, second;
	rtc_get_time(&year, &month, &day, &hour, &minute, &second);
	if(year >= 1970)
		year -= 1970;
	else
		year += 30;
	return (size_t) year * 31536000 + (size_t) month * 2592000 /* 30 days as average number of days per month (this doesnt have to be accurate) */
		+ (size_t) day * 86400 + (size_t) hour * 3600 + (size_t) minute * 60 + (size_t) second;
}



void __attribute__ ((interrupt)) x86_on_irq0(idt_interrupt_frame* frame){
	pic_hw_int();
	pit_irq_tick();
	if(timerHandler)
		timerHandler();
	pic_send_eoi(0);
}




uint8_t x86_inb(uint16_t port){
	uint8_t data;
	asm("mov %1, %%dx; \
		in %%dx, %%al; \
		mov %%al, %0;"
		: "=r" (data)
		: "r" (port)
		: "%dx");
	return data;
}

uint32_t x86_ind(uint16_t port){
	uint32_t data;
	asm("mov %1, %%dx; \
		in %%dx, %%eax; \
		mov %%eax, %0;"
		: "=r" (data)
		: "r" (port)
		: "%dx");
	return data;
}

void x86_outb(uint16_t port, uint8_t data){
	asm("mov %0, %%dx; \
		mov %1, %%al; \
		out %%al, %%dx;"
		:
		: "r" (port), "r" (data)
		: "%dx", "%al");
}

void x86_outd(uint16_t port, uint32_t data){
	asm("mov %0, %%dx; \
		mov %1, %%eax; \
		out %%eax, %%dx;"
		:
		: "r" (port), "r" (data)
		: "%dx", "%eax");
}

#if defined(ARCH_amd64)
void x86_loadGDT(size_t address){
	asm("mov %0, %%rdx; \
		lgdt (%%rdx);"
		:
		: "r" (address)
		: "%rdx");
}

void x86_loadIDT(size_t address){
	asm("mov %0, %%rdx; \
		lidt (%%rdx);"
		:
		: "r" (address)
		: "%rdx");
}

void x86_set_segments(uint16_t codeSegment, uint16_t dataSegment){
	asm("movzx %0, %%rax; \
		mov %%ax, %%ds; \
		mov %%ax, %%es; \
		mov %%ax, %%fs; \
		mov %%ax, %%gs; \
		mov %%ax, %%ss;"
		:
		: "r" (dataSegment)
		: "%rax");
	// 0x5 is the size of the 3 pushs (1 byte each) and iretq (2 bytes)
	asm("movzx %0, %%rax; \
		lea 0x5(%%rip), %%rcx; \
		push %%rax; \
		push %%rcx; \
		lretq; \
		nop; \
		nop; \
		nop; \
		nop; \
		nop;"
		:
		: "r" (codeSegment)
		: "%rax", "%rcx");
}
#elif defined(ARCH_i386)
void x86_loadGDT(size_t address){
	asm("mov %0, %%edx; \
		lgdt (%%edx);"
		:
		: "r" (address)
		: "%edx");
}

void x86_loadIDT(size_t address){
	asm("mov %0, %%edx; \
		lidt (%%edx);"
		:
		: "r" (address)
		: "%edx");
}

void x86_set_segments(uint16_t codeSegment, uint16_t dataSegment){
	asm("movzx %0, %%eax; \
		mov %%ax, %%ds; \
		mov %%ax, %%es; \
		mov %%ax, %%fs; \
		mov %%ax, %%gs; \
		mov %%ax, %%ss;"
		:
		: "r" (dataSegment)
		: "%eax");
	// 0x5 is the size of the 3 pushs (1 byte each) and iretq (2 bytes)
	asm("movzx %0, %%eax; \
		calll %=f; \
		%=: \
		pop %%ecx; \
		lea 0x9(%%ecx), %%ecx; \
		push %%eax; \
		push %%ecx; \
		lret; \
		nop; \
		nop; \
		nop; \
		nop; \
		nop;"
		:
		: "r" (codeSegment)
		: "%rax", "%rcx");
}
#else
#error Invalid x86 architecture
#endif

