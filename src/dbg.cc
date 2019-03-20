/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <dbg/dbg.h>
#include <common/c++/new.h>
#include <common/misc.h>

void
dbg::Debugger::ReadMemory(addr_t addr, int len, void *buf, error *err)
{
   std::vector<Breakpoint*> bps;

   if (!len)
      goto exit;

   this->bps.FindBreakpointsInRange(bps, addr, len, err);
   ERROR_CHECK(err);

   proc->ReadMemory(addr, len, buf, err);
   ERROR_CHECK(err);

   for (auto bp : bps)
   {
      auto start = MAX(addr, bp->vaddr);
      auto end = MIN(addr+len, bp->vaddr + bp->size);
      if (start < end)
      {
         memcpy(
            (char*)buf + (start - addr),
            (char*)bp->OldText() + (start - bp->vaddr),
            end - start
         );
      }
   }
exit:;
}

void
dbg::Debugger::WriteMemory(addr_t addr, int len, const void *buf, error *err)
{
   std::vector<Breakpoint*> bps;

   if (!len)
      goto exit;

   this->bps.FindBreakpointsInRange(bps, addr, len, err);
   ERROR_CHECK(err);

   for (auto bp : bps)
   {
      int prefixLen = 0;
      int off = 0;

      if (bp->vaddr > addr)
      {
         prefixLen = bp->vaddr - addr;

         proc->WriteMemory(addr, prefixLen, buf, err);
         ERROR_CHECK(err);

         addr += prefixLen;
         len -= prefixLen;
         buf = (char*)buf + prefixLen;
      }
      else if (bp->vaddr < addr)
      {
         off = addr - bp->vaddr;
      }

      prefixLen = MIN(len, bp->size - off);
      memcpy((char*)bp->OldText() + off, buf, prefixLen);

      addr += prefixLen;
      len -= prefixLen;
      buf = (char*)buf + prefixLen;

      if (!len)
         goto exit;   
   }

   proc->WriteMemory(addr, len, buf, err);
   ERROR_CHECK(err);
exit:;
}

void
dbg::Debugger::Detach(error *err)
{
   // Clear breakpoints
   //
   for (auto &bp : bps.bps)
   {
      proc->WriteMemory(bp->vaddr, bp->size, bp->OldText(), err);
      ERROR_CHECK(err);
   }

   proc->Detach(err);
   ERROR_CHECK(err);
exit:;
}

dbg::Breakpoint *
dbg::Debugger::GetCurrentBreakpoint(error *err)
{
   dbg::Breakpoint *r = nullptr;
   uintptr_t pc = 0;

   pc = cpu->GetPc(proc.Get(), err);
   ERROR_CHECK(err);

   r = bps.Lookup(pc);
exit:
   return r;
}

void
dbg::Debugger::Step(error *err)
{
   auto bp = GetCurrentBreakpoint(err);
   ERROR_CHECK(err);

   // If this is a breakpoint, we'll want to revert the patch.
   //
   if (bp)
   {
      proc->WriteMemory(bp->vaddr, bp->size, bp->OldText(), err);
      ERROR_CHECK(err);
   }

   proc->Step(err);
   ERROR_CHECK(err);

   // Re-patch bp.
   //
   if (bp)
   {
      proc->WriteMemory(bp->vaddr, bp->size, bp->PatchedText(), err);
      ERROR_CHECK(err);
   }
exit:;
}

void
dbg::Debugger::Go(error *err)
{
   auto bp = GetCurrentBreakpoint(err);
   ERROR_CHECK(err);

   // Step over breakpoint.
   //
   if (bp)
   {
      Step(err);
      ERROR_CHECK(err);

      // If the new PC is a breakpoint, stop now.
      //
      if (GetCurrentBreakpoint(err) || ERROR_FAILED(err))
         goto exit;
   }

   proc->Go(err);
   ERROR_CHECK(err);
exit:;
}

void
dbg::Debugger::SetBreakpoint(addr_t pc, error *err)
{
   unsigned char text[16];   // XXX should be enough to fit x86 instructions
   bool read = false;
   Breakpoint *bp = nullptr;
   int size = 0;

   if (!(size = cpu->GetFixedSizedBreakpoint()))
   {
      ReadMemory(pc, sizeof(text), text, err);
      ERROR_CHECK(err);
      read = true;

      size = cpu->GetInstructionLength(text, sizeof(text));
      if (size <= 0)
         ERROR_SET(err, unknown, "Couldn't determine instruction length");
   }

   if (!read)
   {
      ReadMemory(pc, size, text, err);
      ERROR_CHECK(err);
   }

   bp = bps.Insert(pc, size, err);
   ERROR_CHECK(err);

   memcpy(bp->OldText(), text, size);
   cpu->GenerateBreakpoint(pc, bp->PatchedText(), size, err);
   ERROR_CHECK(err);

   proc->WriteMemory(pc, size, bp->PatchedText(), err);
   ERROR_CHECK(err);

exit:
   if (ERROR_FAILED(err) && bp)
   {
      bps.bps.erase(bps.bps.end()-1);
   }
}

void
dbg::Debugger::DeleteBreakpoint(int idx, error *err)
{
   Breakpoint *bp = nullptr;

   if (idx < 0 || idx >= bps.bps.size())
      ERROR_SET(err, unknown, "Invalid index");

   bp = bps.bps[idx].get();

   proc->WriteMemory(bp->vaddr, bp->size, bp->OldText(), err);
   ERROR_CHECK(err);

   bps.bps.erase(bps.bps.begin() + idx);
exit:;
}

void
dbg::Create(
   Debugger **p,
   error *err
)
{
   common::Pointer<Debugger> r;
   common::New(r, err);
   ERROR_CHECK(err);
   Create(r->proc.GetAddressOf(), err);
   ERROR_CHECK(err);
   New(r->proc->EventCallbacks, err);
   ERROR_CHECK(err);
   Create(r->cpu.GetAddressOf(), err);
   ERROR_CHECK(err);
   r->proc->Cpu = r->cpu;
exit:
   if (ERROR_FAILED(err))
      r = nullptr;
   *p = r.Detach();
}
