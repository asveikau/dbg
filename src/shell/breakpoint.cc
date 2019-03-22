#include <dbg/shell.h>

void
dbg::shell::RegisterBpCommands(CommandList &list, error *err)
{
   list["bp"] = [] (CommandState &st, error *err) -> void
   {
      addr_t addr = 0;

      if (st.argv.size() < 2)
         ERROR_SET(err, unknown, "usage: bp <addr>");

      addr = st.ParseAddress(1, err);
      ERROR_CHECK(err);

      st.dbg->SetBreakpoint(addr, err);
      ERROR_CHECK(err);
   exit:;
   };

   list["bl"] = [] (CommandState &st, error *err) -> void
   {
      int idx = 0;

      for (auto &bp : st.dbg->bps.bps)
      {
         char buf[128];
         const char *addr = FormatAddr(st, bp->vaddr, buf, sizeof(buf), err);
         ERROR_CHECK(err);
         st.dbg->proc->EventCallbacks->OnMessage(err, "0x%.2x %s\n", idx++, addr);
         ERROR_CHECK(err);
      }
   exit:;
   };

   list["bc"] = [] (CommandState &st, error *err) -> void
   {
      int idx = 0;

      if (st.argv.size() < 2)
         ERROR_SET(err, unknown, "usage: bc <idx>");

      st.ParseBinaryArg(1, &idx, sizeof(idx), err);
      ERROR_CHECK(err);

      st.dbg->DeleteBreakpoint(idx, err);
      ERROR_CHECK(err);
   exit:;
   };
}
