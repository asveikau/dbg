/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef dbg_arch_x86_h
#define dbg_arch_x86_h

#if !defined(__amd64__)

// 32-bit registers...
//
#define DBG_EVAL_REGISTER(regno, macro)         \
   do                                           \
   {                                            \
      switch (regno)                            \
      {                                         \
      case DBG_AX:    { macro(eax); break; }    \
      case DBG_BX:    { macro(ebx); break; }    \
      case DBG_CX:    { macro(ecx); break; }    \
      case DBG_DX:    { macro(edx); break; }    \
      case DBG_SI:    { macro(esi); break; }    \
      case DBG_DI:    { macro(edi); break; }    \
      case DBG_SP:    { macro(esp); break; }    \
      case DBG_BP:    { macro(ebp); break; }    \
      case DBG_IP:    { macro(eip); break; }    \
      case DBG_FLAGS: { macro(eflags); break; } \
      }                                         \
   } while(0)                                   \

#define DBG_REGISTER_COUNT (10) 

#else

// 64-bit registers...
//
#define DBG_EVAL_REGISTER(regno, macro)         \
   do                                           \
   {                                            \
      switch (regno)                            \
      {                                         \
      case DBG_AX:    { macro(rax); break; }    \
      case DBG_BX:    { macro(rbx); break; }    \
      case DBG_CX:    { macro(rcx); break; }    \
      case DBG_DX:    { macro(rdx); break; }    \
      case DBG_SI:    { macro(rsi); break; }    \
      case DBG_DI:    { macro(rdi); break; }    \
      case DBG_SP:    { macro(rsp); break; }    \
      case DBG_BP:    { macro(rbp); break; }    \
      case DBG_IP:    { macro(rip); break; }    \
      case DBG_FLAGS: { macro(rflags); break; } \
      case DBG_R8:    { macro(r8); break; }     \
      case DBG_R9:    { macro(r9); break; }     \
      case DBG_R10:   { macro(r10); break; }    \
      case DBG_R11:   { macro(r11); break; }    \
      case DBG_R12:   { macro(r12); break; }    \
      case DBG_R13:   { macro(r13); break; }    \
      case DBG_R14:   { macro(r14); break; }    \
      case DBG_R15:   { macro(r15); break; }    \
      }                                         \
   } while(0)                                   \

#define DBG_REGISTER_COUNT (0x12) 

#endif

/*
 * Constants to use as "regno".
 *
 * To make portability between 32/64 bit manipulations easier,
 * these names will remove the (r|e) prefix...
 * i.e., AX implies EAX/RAX.
 */
enum
{
   DBG_AX,
   DBG_BX,
   DBG_CX,
   DBG_DX,
   DBG_SI,
   DBG_DI,
   DBG_SP,
   DBG_BP,
   DBG_IP,
   DBG_FLAGS,
#if defined(__amd64__)
   DBG_R8,
   DBG_R9,
   DBG_R10,
   DBG_R11,
   DBG_R12,
   DBG_R13,
   DBG_R14,
   DBG_R15,
#endif
};

#if defined(__FreeBSD__) || defined(__OpenBSD__)

#include <machine/reg.h>

namespace dbg
{
   typedef struct reg reg_t;
}

#define DBG_ACCESS_REG(regname)   (((reg_t*)NULL)->r_##regname)

#elif defined(__linux__)

#include <sys/user.h>

namespace dbg
{
   typedef struct user_regs_struct reg_t;
}
#define eax orig_eax
#define rax orig_rax
#define rflags eflags

#define DBG_ACCESS_REG(regname)   (((reg_t*)NULL)->regname)

#define USE_GETREGS

#elif defined(__APPLE__)

#include <mach/mach_types.h>
#include <mach/machine/thread_status.h>

namespace dbg
{
#if defined(__amd64__)
   typedef x86_thread_state64_t reg_t;
#define DBG_ACCESS_REG(regname)   (((reg_t*)NULL)->__##regname)
#define THREAD_STATE_COUNT        x86_THREAD_STATE64_COUNT
#define THREAD_STATE_FLAVOR       x86_THREAD_STATE64
#else
   typedef i386_thread_state reg_t;
#define DBG_ACCESS_REG(regname)   (((reg_t*)NULL)->##regname)
#define THREAD_STATE_COUNT        x86_THREAD_STATE_COUNT
#define THREAD_STATE_FLAVOR       x86_THREAD_STATE
#endif
}


#endif

#endif
