#include "x86_64.h"
volatile uint64_t neo_x86_64_ticks;
struct neo_interrupt_frame { uint64_t rip,cs,flags,rsp,ss; };
static void __attribute__((interrupt)) neo_timer(struct neo_interrupt_frame *frame) {
    (void)frame; ++neo_x86_64_ticks; neo_out(0x20,0x20);
    *(volatile uint32_t *)(uintptr_t)0xfee000b0=0;
}
extern void neo_fault_entry(void);
typedef struct __attribute__((packed)) { uint16_t low,selector; uint8_t ist,attributes; uint16_t middle; uint32_t high,reserved; } neo_gate;
static neo_gate neo_idt[256];
static uint64_t neo_gdt[]={0,UINT64_C(0x00af9a000000ffff),UINT64_C(0x00cf92000000ffff)};
typedef struct __attribute__((packed)) { uint16_t length; uint64_t address; } neo_table;
static void neo_gate_set(unsigned index, uintptr_t address) {
    neo_idt[index]=(neo_gate){(uint16_t)address,8,0,0x8e,(uint16_t)(address>>16),(uint32_t)(address>>32),0};
}
void neo_x86_64_interrupts(void) {
    __asm__ volatile("cli");
    neo_table gdt={sizeof(neo_gdt)-1,(uintptr_t)neo_gdt};
    __asm__ volatile("lgdt %0\n pushq $8\n leaq 1f(%%rip),%%rax\n pushq %%rax\n lretq\n 1:\n movw $16,%%ax\n movw %%ax,%%ds\n movw %%ax,%%es\n movw %%ax,%%ss"::"m"(gdt):"rax","memory");
    for(unsigned i=0;i<256;++i) { neo_gate_set(i,(uintptr_t)neo_fault_entry); }
    neo_gate_set(32,(uintptr_t)neo_timer);
    neo_table idt={sizeof(neo_idt)-1,(uintptr_t)neo_idt}; __asm__ volatile("lidt %0"::"m"(idt):"memory");
    neo_out(0x20,0x11); neo_out(0xa0,0x11); neo_out(0x21,0x20); neo_out(0xa1,0x28);
    neo_out(0x21,4); neo_out(0xa1,2); neo_out(0x21,1); neo_out(0xa1,1);
    neo_out(0x21,0xfe); neo_out(0xa1,0xff);
    uint16_t divisor=11932; neo_out(0x43,0x36); neo_out(0x40,(unsigned char)divisor); neo_out(0x40,(unsigned char)(divisor>>8));
    volatile uint32_t *apic=(volatile uint32_t *)(uintptr_t)0xfee00000;
    apic[0xf0/4]=0x1ff; apic[0x80/4]=0; apic[0x350/4]=0x700;
    __asm__ volatile("sti");
}
