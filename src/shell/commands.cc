/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <dbg/shell.h>

#include "dump.h"
#include "edit.h"

void
dbg::shell::RegisterCommands(CommandList &list, error *err)
{
   try
   {
      RegisterBpCommands(list, err);
      ERROR_CHECK(err);

      list["db"] = DumpCommand<1>();
      list["dw"] = DumpCommand<2>();
      list["dd"] = DumpCommand<4>();
      list["dq"] = DumpCommand<8>();

      list["eb"] = EditCommand<1>();
      list["ew"] = EditCommand<2>();
      list["ed"] = EditCommand<4>();
      list["eq"] = EditCommand<8>();

      list[".detach"] = [] (CommandState &st, error *err) -> void
      {
         st.dbg->Detach(err);
      };

      list["g"] = [] (CommandState &st, error *err) -> void
      {
         st.dbg->Go(err);
         ERROR_CHECK(err);
         if (st.dbg->proc->IsAttached())
         {
            Disassemble(st, 1, err);
            ERROR_CHECK(err);
         }
      exit:;
      };

      list["k"] = [] (CommandState &st, error *err) -> void
      {
         st.dbg->cpu->StackTrace(
            st.dbg,
            [&st] (addr_t pc, addr_t frame, bool& cancel, error *err) -> void
            {
               if (st.dbg->proc->EventCallbacks.Get())
               {
                  char buf[64];

                  FormatAddr(st, pc, buf, sizeof(buf), err);
                  ERROR_CHECK(err);
                  st.dbg->proc->EventCallbacks->OnMessage(err, "%s\n", buf);
                  ERROR_CHECK(err);
               }
            exit:;
            },
            err
         );
      };

      list["r"] = RegisterCommand();

      list["t"] = [] (CommandState &st, error *err) -> void
      {
         st.dbg->Step(err);
         ERROR_CHECK(err);
         if (st.dbg->proc->IsAttached())
         {
            Disassemble(st, 1, err);
            ERROR_CHECK(err);
         }
      exit:;
      };

      list["q"] = [] (CommandState &st, error *err) -> void
      {
         st.dbg->proc->Quit(err);
         st.quitFlag = true;
      };

      list["u"] = DisassembleCommand();
   }
   catch (std::bad_alloc)
   {
      ERROR_SET(err, nomem);
   }
exit:;
}
