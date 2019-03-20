/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <dbg/process.h>
#include <common/misc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
dbg::ProcessEvents::OnMessage(const char *msg, error *err)
{
   if (msg && *msg)
   {
      size_t len = strlen(msg);
      if (msg[len-1] == '\n')
         fputs(msg, stdout);
      else
         puts(msg);
   }
}

void
dbg::ProcessEvents::OnVMessage(error *err, const char *fmt, va_list ap)
{
   char *buf = nullptr;
   vasprintf(&buf, fmt, ap);
   if (!buf)
      ERROR_SET(err, nomem);
   OnMessage(buf, err);
   ERROR_CHECK(err);
exit:;
   free(buf);
}

void
dbg::ProcessEvents::OnMessage(error *err, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);

   OnVMessage(err, fmt, ap);
   va_end(ap);
}

