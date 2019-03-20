/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

namespace {

template<int size>
dbg::shell::Command
EditCommand()
{
   return [] (dbg::shell::CommandState &st, error *err) -> void
   {
      dbg::addr_t addr = 0;
      size_t bufsz = 0;
      unsigned char *buf = nullptr;
      unsigned char *p = nullptr;

      if (st.argv.size() < 3)
      {
         ERROR_SET(err, unknown, "edit: requires address and value");
      }

      addr = st.ParseAddress(1, err);
      ERROR_CHECK(err);

      bufsz = (st.argv.size() - 2) * size;
      try
      {
         buf = new unsigned char[bufsz];
      }
      catch (std::bad_alloc)
      {
         ERROR_SET(err, nomem);
      }
      p = buf;

      for (int i=2; i<st.argv.size(); ++i)
      {
         st.ParseBinaryArg(st.argv[i].c_str(), p, size, err);
         ERROR_CHECK(err);

         p += size;
      }

      st.dbg->WriteMemory(addr, bufsz, buf, err);
      ERROR_CHECK(err);

   exit:
      if (buf)
         delete [] buf;
   };
}

} // end namespace