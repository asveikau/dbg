/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef dbg_process_h_
#define dbg_process_h_

#include "types.h"

#include <stdarg.h>

namespace dbg {

struct ProcessEvents : public virtual common::RefCountable
{
   virtual void OnMessage(const char *str, error *err);
   virtual void OnProcessExited(error *err) {}
   virtual void OnSignal(int sig, error *err) {}
   virtual void OnModuleProbed(addr_t baseAddr, const char *optName, error *err) {}

   void OnMessage(error *err, const char *fmt, ...);
   void OnVMessage(error *err, const char *fmt, va_list ap);
};

struct Process : public common::RefCountable
{
   common::Pointer<ProcessEvents> EventCallbacks;
   common::Pointer<Cpu> Cpu;

   virtual void
   Attach(const char *string, error *err) = 0;

   virtual void
   Create(char *const* argv, error *err) = 0;

   // A recommended block size for memory transfers.
   // In the traditional ptrace, this is going to be quite small,
   // eg. a machine word.
   //
   virtual int
   GetBlockSize() { return 256; }

   virtual void
   ReadMemory(addr_t addr, int len, void *buf, error *err) = 0;

   virtual void
   WriteMemory(addr_t addr, int len, const void *buf, error *err) = 0;

   virtual void
   GetRegister(int regno, void *reg, error *err) = 0;

   virtual void
   SetRegister(int regno, const void *reg, error *err) = 0;

   virtual void
   Step(error *err) = 0;

   virtual void
   Go(error *err) = 0;

   virtual void
   Interrupt(error *err) = 0;

   virtual void
   Detach(error *err) = 0;

   virtual void
   Quit(error *err) = 0;
};

void
Create(
   Process **p,
   error *err
);

}

#include <dbg/cpu.h>

#endif
