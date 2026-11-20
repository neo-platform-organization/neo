#ifndef NEO_X86_64_PLATFORM_H
#define NEO_X86_64_PLATFORM_H
#include <stddef.h>
#include <stdint.h>
void neo_x86_64_serial(const char *);
void neo_x86_64_serial_byte(unsigned char);
void neo_x86_64_panic(const char *) __attribute__((noreturn));
void neo_x86_64_heap(void *,size_t);
void neo_x86_64_interrupts(void);
extern volatile uint64_t neo_x86_64_ticks;
extern const char *neo_x86_64_image;
static inline unsigned char neo_in(unsigned short port) { unsigned char value; __asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port)); return value; }
static inline void neo_out(unsigned short port, unsigned char value) { __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port)); }
static inline uint64_t neo_tsc(void) { uint32_t lo,hi; __asm__ volatile("rdtsc":"=a"(lo),"=d"(hi)); return ((uint64_t)hi<<32)|lo; }
#endif
