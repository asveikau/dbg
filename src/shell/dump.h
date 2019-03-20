/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <ctype.h>

namespace {

template<int size>
dbg::shell::Command
DumpCommand()
{
   return [] (dbg::shell::CommandState &st, error *err) -> void
   {
      uintptr_t addr = 0;

      char buf[81];
      char *addr_end = buf + sizeof(addr)*2;
      char *raw_bytes = NULL;
      size_t content_size = sizeof(buf) - 1 - (sizeof(addr)*2+1);
      size_t bytes_per_unit = size * 2 + 1 + (size == 1 ? 1 : 0);
      size_t units_per_line = content_size / bytes_per_unit;
      size_t lines = 10;
      void *input_heap = NULL;
      const unsigned char *input;

      if (st.argv.size() < 2)
      {
         // TODO - add to previous address after repeated calls

         ERROR_SET(err, unknown, "dump: requires address");
      }

      addr = st.ParseAddress(1, err);
      ERROR_CHECK(err);

      memset(buf, ' ', sizeof(buf) - 1);
      buf[sizeof(buf)-1] = 0;

      *addr_end-- = '|';

      if (size == 1)
      {
         raw_bytes = buf + sizeof(buf) - 1 - units_per_line; 
         raw_bytes[-1] = '|';
      }

      input_heap = malloc(units_per_line * size * lines);
      if (input_heap)
      {
         input = (unsigned char*)input_heap;

         st.dbg->ReadMemory(addr, units_per_line * size * lines, input_heap, err);
         ERROR_CHECK(err);
      }

      while (lines)
      {
         char *p = addr_end;
         uintptr_t a = addr;
         size_t n = units_per_line;
         static const char digits[] = "0123456789abcdef";

         while (p >= buf)
         {
            *p-- = digits[a & 0xf];
            a >>= 4;
            *p-- = digits[a & 0xf];
            a >>= 4;
         }

         if (size == 1)
         {
            const unsigned char *q = input;
            p = raw_bytes;

            while (q < input + units_per_line)
            {
               *p++ = ((*q & 0x80) || !isprint(*q)) ? '.' : *q;
               ++q;
            }
         }

         p = addr_end + 2;
         while (n--)
         {
            const char *fmt = NULL;
            union
            {
               uint32_t u4;
               uint64_t u8;
            } arg;

            switch (size)
            {
            case 1:
               fmt = "%.2" PRIx32;
               arg.u4 = *(unsigned char*)input;
               break;
            case 2:
               fmt = "%.4" PRIx32;
               arg.u4 = *(uint16_t*)input;
               break;
            case 4:
               fmt = "%.8" PRIx32;
               arg.u4 = *(uint32_t*)input;
               break;
            case 8:
               fmt = "%.16" PRIx64;
               arg.u8 = *(uint64_t*)input;
               break;
            }
            if (fmt)
            {
               char sz[size * 2 + 1];
               size_t l;
               snprintf(sz, sizeof(sz), fmt, arg);
               memcpy(p, sz, l = strlen(sz));
               p += l;
               *p++ = ' ';
            }
            input += size;
         }

         if (st.dbg->proc->EventCallbacks.Get())
         {
            st.dbg->proc->EventCallbacks->OnMessage(err, "%s\n", buf);
            ERROR_CHECK(err);
         }

         --lines;
         addr += units_per_line * size;
      }

   exit:
      free(input_heap);
   };
}

} // end namespace