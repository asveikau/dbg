/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <dbg/shell.h>
#include <common/path.h>
#include <common/logger.h>
#include <common/getopt.h>

#include <readline/readline.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

using namespace dbg::shell;

template<typename T>
void
free_and_clear(T *&p)
{
   free(p);
   p = nullptr;
}

static void
usage()
{
   const char *progname, *p;
   error err;

   progname = get_program_path(&err);
   ERROR_CHECK(&err);
   while ((p = strpbrk(progname, PATH_SEP_PBRK)))
      progname = p+1;

   fprintf(stderr, "usage: %s <-p pid|cmdline ...>\n", progname);

exit:
   exit(1);
}

static
dbg::Debugger
*dbgPtr = nullptr;

static void
sigint(int sig)
{
   if (dbgPtr && dbgPtr->proc.Get())
   {
      error err;
      dbgPtr->proc->Interrupt(&err);
   }
}

int
main(int argc, char **argv)
{
   error err;
   char *line = nullptr;
   int c;
   const char *pid = nullptr;
   struct sigaction sa;

   common::Pointer<dbg::Debugger> dbg;
   CommandState state;
   CommandList cmds;

   libcommon_set_argv0(*argv);
   log_register_callback(
      [] (void *ctx, const char *msg) -> void
      {
         fputs(msg, stderr);
      },
      nullptr
   );
   register_backtrace_logger(NULL, NULL);

   memset(&sa, 0, sizeof(sa));
   sigemptyset(&sa.sa_mask);
   sa.sa_handler = SIG_IGN;

   if (sigaction(SIGTTOU, &sa, nullptr))
      ERROR_SET(&err, errno, errno);

   memset(&sa, 0, sizeof(sa));
   sigemptyset(&sa.sa_mask);
   sa.sa_handler = sigint;

   if (sigaction(SIGINT, &sa, nullptr))
      ERROR_SET(&err, errno, errno);

   while ((c = getopt(argc, argv, "p:")) != -1)
   {
      switch (c)
      {
      case 'p':
         pid = optarg;
         break;
      default:
         usage();
      }
   }

   argc -= optind;
   argv += optind;

   if (!pid && !argc)
      usage();

   dbg::Create(dbg.GetAddressOf(), &err);
   ERROR_CHECK(&err);

   if (pid)
      dbg->proc->Attach(pid, &err);
   else
      dbg->proc->Create(argv, &err);
   ERROR_CHECK(&err);

   state.dbg = dbgPtr = dbg.Get();

   Disassemble(state, 1, &err);
   ERROR_CHECK(&err);

   RegisterCommands(cmds, &err);
   ERROR_CHECK(&err);

   for (; !state.quitFlag && (line = readline("dbg> ")); free_and_clear(line))
   {
      state.Parse(line, &err);
      ERROR_CHECK(&err);

      if (!state.argv.size())
         continue;

      auto i = cmds.find(state.argv[0]);
      if (i != cmds.end())
      {
         i->second(state, &err);
         if (ERROR_FAILED(&err))
            error_clear(&err);
      }
      else
      {
         fprintf(stderr, "Unrecognized command \"%s\"\n", state.argv[0].c_str());
      }
   }

   dbgPtr = nullptr;

exit:
   free(line);
   return 0;   
}
