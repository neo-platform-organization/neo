#include "../../platform.h"
#include "x86_64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>

typedef struct neo_heap_block { size_t size; struct neo_heap_block *next; uint64_t used, guard; } neo_heap_block;
static neo_heap_block *neo_heap;
void neo_x86_64_heap(void *memory, size_t length) {
    neo_heap=memory; *neo_heap=(neo_heap_block){length-sizeof(*neo_heap),NULL,0,0x4e454f};
}
static void *neo_heap_take(size_t length) {
    if (length>SIZE_MAX-15) { return NULL; } length=(length+15u)&~(size_t)15;
    for (neo_heap_block *block=neo_heap; block; block=block->next) {
        if (block->used || block->size<length) { continue; }
        if (block->size>=length+sizeof(*block)+16) {
            neo_heap_block *next=(neo_heap_block *)((unsigned char *)(block+1)+length);
            *next=(neo_heap_block){block->size-length-sizeof(*block),block->next,0,0x4e454f}; block->next=next; block->size=length;
        }
        block->used=1; return block+1;
    }
    return NULL;
}
void *neo_platform_allocate(size_t length, size_t alignment) {
    if (alignment<sizeof(void *) || alignment>4096 || (alignment & (alignment-1)) || length>SIZE_MAX-alignment-32) { return NULL; }
    void *raw=neo_heap_take(length+alignment+16); if (!raw) { return NULL; }
    uintptr_t address=((uintptr_t)raw+16+alignment-1)&~(uintptr_t)(alignment-1);
    ((void **)address)[-2]=raw; ((size_t *)address)[-1]=length; return (void *)address;
}
void *neo_platform_malloc(size_t length) { return neo_platform_allocate(length ? length : 1,16); }
void *neo_platform_calloc(size_t count, size_t bytes) {
    if (count && bytes>SIZE_MAX/count) { return NULL; }
    size_t length=count*bytes; void *memory=neo_platform_malloc(length); if (memory) { memset(memory,0,length); } return memory;
}
void neo_platform_free(void *memory) {
    if (!memory) { return; } void *raw=((void **)memory)[-2]; neo_heap_block *block=(neo_heap_block *)raw-1;
    if (block->guard!=0x4e454f || !block->used) { neo_x86_64_panic("invalid heap release"); }
    block->used=0;
    for (block=neo_heap; block && block->next;) {
        if (!block->used && !block->next->used) { block->size+=sizeof(*block)+block->next->size; block->next=block->next->next; }
        else { block=block->next; }
    }
}
void *neo_platform_realloc(void *memory, size_t length) {
    if (!memory) { return neo_platform_malloc(length); }
    void *next=neo_platform_malloc(length); if (!next) { return NULL; }
    size_t previous=((size_t *)memory)[-1]; memcpy(next,memory,previous<length ? previous : length); neo_platform_free(memory); return next;
}
void *malloc(size_t n) { return neo_platform_malloc(n); }
void *calloc(size_t n,size_t s) { return neo_platform_calloc(n,s); }
void *realloc(void *p,size_t n) { return neo_platform_realloc(p,n); }
void free(void *p) { neo_platform_free(p); }
void *memmove(void *out,const void *in,size_t n) { unsigned char *d=out; const unsigned char *s=in;
    if ((uintptr_t)d<(uintptr_t)s) { for(size_t i=0;i<n;++i) { d[i]=s[i]; } }
    else { while(n) { --n; d[n]=s[n]; } } return out; }
int memcmp(const void *a,const void *b,size_t n) { const unsigned char *x=a,*y=b; for(size_t i=0;i<n;++i) { if(x[i]!=y[i]) { return x[i]<y[i] ? -1:1; } } return 0; }
void *memchr(const void *p,int c,size_t n) { const unsigned char *s=p; for(size_t i=0;i<n;++i) { if(s[i]==(unsigned char)c) { return (void *)(s+i); } } return NULL; }
size_t strlen(const char *s) { size_t n=0; while(s[n]) { ++n; } return n; }
int strcmp(const char *a,const char *b) { while(*a && *a==*b) { ++a; ++b; } return (unsigned char)*a-(unsigned char)*b; }
int strncmp(const char *a,const char *b,size_t n) { for(size_t i=0;i<n;++i) { if(a[i]!=b[i] || !a[i]) { return (unsigned char)a[i]-(unsigned char)b[i]; } } return 0; }
char *strcpy(char *a,const char *b) { memcpy(a,b,strlen(b)+1); return a; }
char *strchr(const char *s,int c) { do { if(*s==(char)c) { return (char *)s; } } while(*s++); return NULL; }
char *strrchr(const char *s,int c) { const char *last=NULL; do { if(*s==(char)c) { last=s; } } while(*s++); return (char *)last; }
size_t strspn(const char *s,const char *set) { size_t n=0; while(s[n] && strchr(set,s[n])) { ++n; } return n; }
size_t strcspn(const char *s,const char *set) { size_t n=0; while(s[n] && !strchr(set,s[n])) { ++n; } return n; }
char *strtok(char *s,const char *set) { static char *next; if(s) { next=s; } if(!next) { return NULL; }
    next+=strspn(next,set); if(!*next) { next=NULL; return NULL; } char *first=next; next+=strcspn(next,set); if(*next) { *next++=0; } else { next=NULL; } return first; }
static int neo_errno;
int *__errno_location(void) { return &neo_errno; }
static uintmax_t neo_unsigned(const char *text,char **end,int base,bool *negative) {
    const char *start=text; while(*text==' ' || *text=='\t' || *text=='\n') { ++text; }
    *negative=*text=='-'; if(*text=='+' || *text=='-') { ++text; }
    if(base!=10 && base!=0) { if(end) { *end=(char *)start; } errno=EINVAL; return 0; }
    uintmax_t value=0; bool any=false;
    while(*text>='0' && *text<='9') { unsigned digit=(unsigned)(*text-'0');
        if(value>(UINTMAX_MAX-digit)/10) { errno=ERANGE; value=UINTMAX_MAX; } else { value=value*10+digit; }
        any=true; ++text; }
    if(end) { *end=(char *)(any ? text : start); } return value;
}
uintmax_t strtoumax(const char *s,char **e,int b) { bool negative; uintmax_t v=neo_unsigned(s,e,b,&negative); return negative ? (uintmax_t)0-v:v; }
intmax_t strtoimax(const char *s,char **e,int b) { bool negative; uintmax_t v=neo_unsigned(s,e,b,&negative);
    uintmax_t limit=(uintmax_t)INTMAX_MAX+(negative ? 1u:0u); if(v>limit) { errno=ERANGE; return negative ? INTMAX_MIN:INTMAX_MAX; }
    return negative && v ? -(intmax_t)(v-1)-1:(intmax_t)v; }
long strtol(const char *s,char **e,int b) { return (long)strtoimax(s,e,b); }
unsigned long strtoul(const char *s,char **e,int b) { return (unsigned long)strtoumax(s,e,b); }
long long strtoll(const char *s,char **e,int b) { return (long long)strtoimax(s,e,b); }
unsigned long long strtoull(const char *s,char **e,int b) { return (unsigned long long)strtoumax(s,e,b); }
double strtod(const char *s,char **end) { if(end) { *end=(char *)s; } errno=ENOTSUP; return 0; }
double fabs(double value) { uint64_t bits;memcpy(&bits,&value,sizeof(bits));bits&=UINT64_C(0x7fffffffffffffff);memcpy(&value,&bits,sizeof(value));return value; }
static void neo_format_byte(char *out,size_t capacity,size_t *length,char c) { if(*length+1<capacity) { out[*length]=c; } ++*length; }
int vsnprintf(char *out,size_t capacity,const char *format,va_list arguments) {
    size_t length=0;
    while(*format) {
        if(*format!='%') { neo_format_byte(out,capacity,&length,*format++); continue; }
        ++format; if(*format=='%') { neo_format_byte(out,capacity,&length,*format++); continue; }
        unsigned width=0; char padding=' '; if(*format=='0') { padding='0'; ++format; }
        while(*format>='0' && *format<='9') { width=width*10+(unsigned)(*format++-'0'); }
        if(*format=='.') { if(capacity) { out[0]=0; } return -1; }
        unsigned size=0; if(*format=='z') { size=2; ++format; } else { while(*format=='l') { ++size; ++format; } }
        char type=*format++;
        if(type=='s') { const char *text=va_arg(arguments,const char *); if(!text) { text="(null)"; }
            while(*text) { neo_format_byte(out,capacity,&length,*text++); } continue; }
        if(type=='c') { neo_format_byte(out,capacity,&length,(char)va_arg(arguments,int)); continue; }
        bool negative=false; uint64_t value;
        if(type=='d' || type=='i') { int64_t signed_value=size ? va_arg(arguments,int64_t):va_arg(arguments,int);
            negative=signed_value<0; value=negative ? (uint64_t)(-(signed_value+1))+1u:(uint64_t)signed_value; }
        else if(type=='u' || type=='x' || type=='X') { value=size ? va_arg(arguments,uint64_t):va_arg(arguments,unsigned); }
        else { if(capacity) { out[0]=0; } return -1; }
        char digits[32]; size_t count=0; unsigned base=type=='x' || type=='X' ? 16:10;
        do { unsigned digit=(unsigned)(value%base); digits[count++]=(char)(digit<10 ? '0'+digit:(type=='X' ? 'A':'a')+digit-10); value/=base; } while(value);
        if(negative) { neo_format_byte(out,capacity,&length,'-'); }
        while(width>count+(negative ? 1u:0u)) { neo_format_byte(out,capacity,&length,padding); --width; }
        while(count) { neo_format_byte(out,capacity,&length,digits[--count]); }
    }
    if(capacity) { out[length<capacity ? length:capacity-1]=0; } return length>INT_MAX ? -1:(int)length;
}
int snprintf(char *out,size_t n,const char *format,...) { va_list args; va_start(args,format); int result=vsnprintf(out,n,format,args); va_end(args); return result; }
FILE *stdin=(FILE *)1,*stdout=(FILE *)2,*stderr=(FILE *)3;
int fputs(const char *s,FILE *file) { (void)file; neo_x86_64_serial(s); return 0; }
int puts(const char *s) { neo_x86_64_serial(s); neo_x86_64_serial_byte('\n'); return 0; }
static int neo_serial_format(const char *format,va_list args) {
    va_list copy;va_copy(copy,args);int length=vsnprintf(NULL,0,format,copy);va_end(copy);
    if(length<0) { return length; }char *buffer=malloc((size_t)length+1);if(!buffer) { return -1; }
    int result=vsnprintf(buffer,(size_t)length+1,format,args);if(result>=0) { neo_x86_64_serial(buffer); }free(buffer);return result;
}
int printf(const char *format,...) { va_list args;va_start(args,format);int result=neo_serial_format(format,args);va_end(args);return result; }
int fprintf(FILE *file,const char *format,...) { (void)file;va_list args;va_start(args,format);int result=neo_serial_format(format,args);va_end(args);return result; }
int fflush(FILE *file) { (void)file; return 0; }
typedef struct { size_t position; bool eof; } neo_file;
FILE *fopen(const char *path,const char *mode) { if(strcmp(mode,"rb") || strcmp(path,"neo/neo-os/image.neo")) { return NULL; }
    return calloc(1,sizeof(FILE)>sizeof(neo_file) ? sizeof(FILE):sizeof(neo_file)); }
size_t fread(void *out,size_t size,size_t count,FILE *file) { neo_file *input=(neo_file *)file;
    if(!size || count>SIZE_MAX/size) { return 0; } size_t available=strlen(neo_x86_64_image)-input->position;
    size_t bytes=size*count; if(bytes>available) { bytes=available; input->eof=true; }
    memcpy(out,neo_x86_64_image+input->position,bytes); input->position+=bytes; return bytes/size; }
int fgetc(FILE *file) { neo_file *input=(neo_file *)file; if(!neo_x86_64_image[input->position]) { input->eof=true; return EOF; } return (unsigned char)neo_x86_64_image[input->position++]; }
int feof(FILE *file) { return ((neo_file *)file)->eof; }
int ferror(FILE *file) { (void)file; return 0; }
int fclose(FILE *file) { free(file); return 0; }
void abort(void) { neo_x86_64_panic("abort"); }
