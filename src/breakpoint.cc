/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <dbg/breakpoint.h>
#include <common/misc.h>

#include <algorithm>

#include <stdlib.h>
#include <string.h>

dbg::Breakpoint::ptr
dbg::Breakpoint::Allocate(int size, error *err)
{
   Breakpoint *bp = nullptr;
   if (size < 0)
      ERROR_SET(err, unknown, "Invalid size");

   bp = (Breakpoint*)malloc(offsetof(Breakpoint, text) + size * 2);
   if (!bp)
      ERROR_SET(err, nomem);

   bp->vaddr = 0;
   bp->size = size;
   memset(bp->text, 0, size*2);
exit:
   return dbg::Breakpoint::ptr(bp, free);
}

std::unique_ptr<dbg::Breakpoint, void(*)(void*)>
dbg::Breakpoint::Null()
{
  return dbg::Breakpoint::ptr(nullptr, [] (void*) -> void {});
}

//
// XXX - it would be ideal to have faster lookups for breakpoints.
// but I will assume nobody will create a lot of them.
//

dbg::Breakpoint *
dbg::BreakpointList::Lookup(addr_t pc)
{
   for (auto &bp : bps)
   {
      if (pc >= bp->vaddr && pc < bp->vaddr + bp->size)
      {
         return bp.get();
      }
   }

   return nullptr;
}

void
dbg::BreakpointList::FindBreakpointsInRange(
   std::vector<Breakpoint*> &output,
   addr_t addr,
   size_t len,
   error *err
)
{
   output.clear();

   for (auto &bp : bps)
   {
      auto start = MAX(addr, bp->vaddr);
      auto end = MIN(addr + len, bp->vaddr + bp->size);
      if (start < end)
      {
         try
         {
            output.push_back(bp.get());
         }
         catch (std::bad_alloc)
         {
            ERROR_SET(err, nomem);
         }
      }
   }

   std::sort(
      output.begin(),
      output.end(),
      [] (Breakpoint *a, Breakpoint *b) -> bool { return a->vaddr < b->vaddr; }
   );
exit:;
}

dbg::Breakpoint *
dbg::BreakpointList::Insert(
   addr_t addr,
   int len,
   error *err
)
{
   std::vector<Breakpoint*> existing;
   Breakpoint *p = nullptr;
   auto n = Breakpoint::Null();

   FindBreakpointsInRange(existing, addr, len, err);
   ERROR_CHECK(err);

   if (existing.size())
      ERROR_SET(err, unknown, "Proposed breakpoint overlaps with existing bp");

   n = Breakpoint::Allocate(len, err);
   ERROR_CHECK(err);

   p = n.get();
   p->vaddr = addr;
   p->size = len;

   try
   {
      bps.push_back(std::move(n));
   }
   catch (std::bad_alloc)
   {
      ERROR_SET(err, nomem);
   }

exit:
   if (ERROR_FAILED(err))
      p = nullptr;
   return p;
}
