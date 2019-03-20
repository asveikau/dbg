/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <dbg/shell.h>

#include <string.h>
#include <ctype.h>

const char *
dbg::shell::FormatAddr(
   CommandState &st,
   addr_t addr,
   char *buf,
   int len,
   error *err
)
{
   // TODO: symbol lookup
   snprintf(buf, len, "%p", (void*)addr);
   return buf;
}

dbg::addr_t
dbg::shell::CommandState::ParseAddress(int argvIdx, error *err)
{
   // TODO: symbol lookup
   addr_t r = 0;
   ParseBinaryArg(argvIdx, &r, sizeof(r), err);
   return r;
}

void
dbg::shell::CommandState::ParseBinaryArg(
   int argvIdx,
   void *buf,
   size_t remaining,
   error *err
)
{
   if (argvIdx < 0 || argvIdx >= argv.size())
      ERROR_SET(err, unknown, "Index out of range");
   ParseBinaryArg(argv[argvIdx].c_str(), buf, remaining, err);
exit:;
}

void
dbg::shell::CommandState::ParseBinaryArg(
   const char *arg,
   void *buf,
   size_t remaining,
   error *err
)
{
   unsigned char *p, *q;
   const char *s;
   int off = 0;
   int shift;

   memset(buf, 0, remaining);

   if (!strncmp(arg, "0x", 2))
      arg += 2;

   if (littleEndian)
   {
      shift = 1;
      p = (unsigned char*)buf;
      q = p + remaining - 1;
   }
   else
   {
      shift = -1;
      q = (unsigned char*)buf;
      p = q + remaining - 1;
   }

   s = arg + strlen(arg) - 1;

   while (s >= arg && (littleEndian ? p <= q : p >= q))
   {
      static const char digits[] = "0123456789abcdef";
      unsigned char ch = (unsigned char)*s--;
      int c = (islower(ch) ? ch : tolower(ch));
      const char *place = strchr(digits, c);

      if (!place)
      {
         ERROR_SET(err, unknown, "Unrecognized hex digit");
      }

      if ((off++ & 1))
      {
         *p |= ((place - digits) << 4);
         p += shift;
      }
      else
      {
         *p |= (place - digits);
      }
   }

   if (s >= arg)
   {
      ERROR_SET(err, unknown, "Not enough space to store value");
   }

exit:;
}

void
dbg::shell::CommandState::Parse(const char *str, error *err)
{
   try
   {
      originalString = str;
      argv.resize(0);

      while (*str)
      {
         std::string nextToken;

         while (isspace(*str))
            ++str;
         while (*str && !isspace(*str))
            nextToken.push_back(*str++);

         if (nextToken.size())
            argv.push_back(std::move(nextToken));
      }
   }
   catch (std::bad_alloc)
   {
      ERROR_SET(err, nomem);
   }
exit:;
}
