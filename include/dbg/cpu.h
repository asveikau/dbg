/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef dbg_cpu_h_
#define dbg_cpu_h_

#include <functional>

#include "types.h"

namespace dbg {

struct Cpu : public common::RefCountable
{
   int
   GetRegisterCount();

   int
   GetRegisterSize(int regno);

   const char *
   GetRegisterName(int regno);

   int
   GetRegisterByName(const char *name);

   int
   GetInstructionLength(const void *text, int len);

   void
   Disassemble(
      Debugger *dbg,
      addr_t text,
      int instrs,
      std::function<void(addr_t addr, const void *instr, int instrlen, const char*, error *err)> callback,
      error *err
   );

   addr_t
   GetPc(
      Process *proc,
      error *err
   );

   void
   GenerateBreakpoint(
      addr_t pc,
      void *buffer,
      int desiredLen,
      error *err
   );

   // If this arch can generate breakpoints irrespective of
   // instruction size, return that size.  If that doen't
   // make sense, return 0.
   //
   int
   GetFixedSizedBreakpoint();

   void
   OnBreakpointBreak(
      Process *proc,
      error *err
   );

   void
   StackTrace(
      Debugger *dbg,
      std::function<void(addr_t pc, addr_t frame, bool& cancel, error *err)> callback,
      error *err
   );
};

void
Create(
   Cpu **r,
   error *err
);

} // end namespace

#endif
