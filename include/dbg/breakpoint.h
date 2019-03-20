/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef dbg_breakpoint_h_
#define dbg_breakpoint_h_

#include "types.h"

#include <memory>
#include <vector>

namespace dbg {

struct Breakpoint
{
   addr_t vaddr;
   int size;
   unsigned char text[];

   typedef
   std::unique_ptr<Breakpoint, void(*)(void*)>
   ptr;

   static
   ptr
   Null();

   static
   ptr
   Allocate(int size, error *err);

   void *
   OldText()
   {
      return text;
   }

   void *
   PatchedText()
   {
      return text + size;
   }
};

struct BreakpointList
{
   std::vector<Breakpoint::ptr> bps;

   Breakpoint *
   Lookup(addr_t pc);

   void
   FindBreakpointsInRange(
      std::vector<Breakpoint*> &output,
      addr_t addr,
      size_t len,
      error *err
   );

   Breakpoint *
   Insert(
      addr_t addr,
      int len,
      error *err
   );
};

}

#endif