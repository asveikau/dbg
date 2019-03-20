/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <dbg/misc.h>

#include <stdio.h>
#include <signal.h>

namespace {

struct signame
{
   int sig;
   const char *name;
};

#define DECLARE(SIG) { SIG, #SIG }
static const struct signame signames[] =
{
   DECLARE(SIGHUP),
   DECLARE(SIGINT),
   DECLARE(SIGQUIT),
   DECLARE(SIGILL),
   DECLARE(SIGTRAP),
   DECLARE(SIGABRT),
   DECLARE(SIGIOT),
   DECLARE(SIGBUS),
   DECLARE(SIGFPE),
   DECLARE(SIGKILL),
   DECLARE(SIGUSR1),
   DECLARE(SIGSEGV),
   DECLARE(SIGUSR2),
   DECLARE(SIGPIPE),
   DECLARE(SIGALRM),
   DECLARE(SIGTERM),
   DECLARE(SIGCHLD),
   DECLARE(SIGCONT),
   DECLARE(SIGSTOP),
   DECLARE(SIGTSTP),
   DECLARE(SIGTTIN),
   DECLARE(SIGTTOU),
   DECLARE(SIGURG),
   DECLARE(SIGXCPU),
   DECLARE(SIGXFSZ),
   DECLARE(SIGVTALRM),
   DECLARE(SIGPROF),
   DECLARE(SIGWINCH),
   DECLARE(SIGIO),
   DECLARE(SIGSYS),
#ifdef SIGSTKFLT
   DECLARE(SIGSTKFLT),
#endif
#ifdef SIGPOLL
   DECLARE(SIGPOLL),
#endif
#ifdef SIGPWR
   DECLARE(SIGPWR),
#endif
#ifdef SIGTHR
   DECLARE(SIGTHR),
#endif
};
#undef DECLARE

} // end namespace

const char *
dbg::FormatSignal(char *buf, size_t sz, int sig)
{
   const struct signame *p = signames;
   const char *name = nullptr;

   while (!name && p->name)
   {
      if (p->sig == sig)
         name = p->name;
      ++p;
   }

   if (name)
      snprintf(buf, sz, "%d (%s)", sig, name);
   else
      snprintf(buf, sz, "%d", sig);

   return buf;
}
