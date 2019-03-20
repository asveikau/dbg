/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef dbg_dbg_h_
#define dbg_dbg_h_

#include <dbg/cpu.h>
#include <dbg/process.h>
#include <dbg/breakpoint.h>

namespace dbg {

struct Debugger : public common::RefCountable
{
   common::Pointer<Process> proc;
   common::Pointer<Cpu> cpu;
   BreakpointList bps;

   // Returns null if the current PC is not a breakpoint.
   //
   Breakpoint *
   GetCurrentBreakpoint(error *err);

   void
   SetBreakpoint(addr_t pc, error *err);

   void
   DeleteBreakpoint(int idx, error *err);

   //
   // For the following calls, you could reach down to ->proc to
   // get the "real" view, but this layer provides the abstraction
   // of breakpoints, faking out code patches and presenting a
   // logical view.
   //

   void
   ReadMemory(addr_t addr, int len, void *buf, error *err);

   void
   WriteMemory(addr_t addr, int len, const void *buf, error *err);

   void
   Step(error *err);

   void
   Go(error *err);

   void
   Detach(error *err);
};

void
Create(Debugger **r, error *err);

} // end namespace

#endif
