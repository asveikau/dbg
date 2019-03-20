/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <dbg/shell.h>
#include <string.h>

static void
ProcessRegister(
   dbg::shell::CommandState &st,
   int regno,
   const char *assignment,
   error *err
)
{
   const char *name = st.dbg->cpu->GetRegisterName(regno);
   int sz = st.dbg->cpu->GetRegisterSize(regno);
   unsigned char buf[sz];
   unsigned char *p;
   char output[32];
   char *dst = output; 
   static const char digits[] = "0123456789abcdef";

   memset(buf, 0, sizeof(buf));

   if (assignment)
   {
      st.ParseBinaryArg(assignment, buf, sizeof(buf), err);
      ERROR_CHECK(err);
      st.dbg->proc->SetRegister(regno, buf, err);
      ERROR_CHECK(err);
   }
   else
   {
      st.dbg->proc->GetRegister(regno, buf, err);
      ERROR_CHECK(err);
   }

   if (st.littleEndian)
   {
      p = buf + sizeof(buf) - 1; 

      while (p >= buf)
      {
         *dst++ = digits[*p >> 4]; 
         *dst++ = digits[*p-- & 0xf];
      }
   }
   else
   {
      p = buf;
      while (p < buf + sizeof(buf))
      {
         *dst++ = digits[*p >> 4]; 
         *dst++ = digits[*p++ & 0xf];
      }
   }

   *dst = 0;

   if (st.dbg->proc->EventCallbacks.Get())
   {
      st.dbg->proc->EventCallbacks->OnMessage(err, "%s\t= %s\n", name, output);
      ERROR_CHECK(err);
   }
exit:;
}

dbg::shell::Command
dbg::shell::RegisterCommand()
{
   return [] (CommandState &st, error *err) -> void
   {
      if (st.argv.size() == 1)
      {
         for (int i = 0, n = st.dbg->cpu->GetRegisterCount(); i<n; ++i)
         {
            ProcessRegister(st, i, nullptr, err);
            ERROR_CHECK(err);
         }
      }
      else
      {
         for (int i=1; i<st.argv.size(); ++i)
         {
            auto reg = st.argv[i].c_str();
            int id = st.dbg->cpu->GetRegisterByName(reg);
            const char *assignment = NULL;

            if (id < 0)
               ERROR_SET(err, unknown, "No such register");

            i++;

            if (i < st.argv.size() &&
                !strcmp(reg = st.argv[i].c_str(), "="))
            {
               i++;
               if (i < st.argv.size())
                  assignment = (reg = st.argv[i].c_str());
               else
               {
                  ERROR_SET(err, unknown, "Expected value after '='");
               }
            }

            ProcessRegister(st, id, assignment, err);
            ERROR_CHECK(err);

            if (i < st.argv.size() &&
                strcmp(reg = st.argv[i].c_str(), ","))
            {
               ERROR_SET(err, unknown, assignment ? "Expected ','" : "Expected ',' or '='");
            }
         }
      }

   exit:;
   };
}