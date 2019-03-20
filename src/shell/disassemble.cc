/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <dbg/shell.h>

void
dbg::shell::Disassemble(
   CommandState &st,
   addr_t pc,
   int instrs,
   error *err
)
{
   auto dbg = st.dbg;

   if (dbg->proc->EventCallbacks.Get())
   {
      char buf[64];
      const char *addrs = FormatAddr(st, pc, buf, sizeof(buf), err);
      ERROR_CHECK(err); 

      dbg->proc->EventCallbacks->OnMessage(err, "%s:\n", addrs);
      ERROR_CHECK(err);
   }

   dbg->cpu->Disassemble(
      dbg,
      pc,
      instrs,
      [dbg] (dbg::addr_t addr, const void *instr, int instrlen, const char *text, error *err) -> void
      {
         char buf[15*2+2];  // XXX max instruction length for x86, plus space and nul.
         char *p;
         const unsigned char *q;
         static const char digits[] = "0123456789abcdef";

         memset(buf, ' ', sizeof(buf)-1);
         buf[sizeof(buf)-1] = 0;

         for (p = buf, q = (const unsigned char*)instr; instrlen--; )
         {
            *p++ = digits[*q >> 4];
            *p++ = digits[*q++ & 0xf];
         }

         if (dbg->proc->EventCallbacks.Get())
            dbg->proc->EventCallbacks->OnMessage(err, "%s %s\n", buf, text);
      },
      err
   );
   ERROR_CHECK(err);
exit:;
}

void
dbg::shell::Disassemble(
   CommandState &st,
   int instrs,
   error *err
)
{
   addr_t addr = st.dbg->cpu->GetPc(st.dbg->proc.Get(), err);
   ERROR_CHECK(err);
   Disassemble(st, addr, instrs, err);
exit:;
}

dbg::shell::Command
dbg::shell::DisassembleCommand()
{
   return [] (CommandState &st, error *err) -> void
   {
      addr_t addr = 0;

      if (st.argv.size() >= 2)
         addr = st.ParseAddress(1, err);
      else
      {
         // TODO: if pc hasn't changed since last time we did this,
         // advance ptr.
         //
         addr = st.dbg->cpu->GetPc(st.dbg->proc.Get(), err);
      }
      ERROR_CHECK(err);

      Disassemble(st, addr, 10, err);
      ERROR_CHECK(err);
   exit:;
   };
}