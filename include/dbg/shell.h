/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef dbg_shell_h_
#define dbg_shell_h_

#include <dbg/dbg.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dbg { namespace shell {

struct CommandState
{
   Debugger *dbg;
   std::vector<std::string> argv;
   std::string originalString;
   bool quitFlag;
   bool littleEndian;

   CommandState() : dbg(nullptr), quitFlag(false)
   {
      static const int x = 1;
      littleEndian = (*(char*)&x) ? true : false;
   }

   void
   Parse(const char *str, error *err);

   addr_t
   ParseAddress(int argvIdx, error *err);

   void
   ParseBinaryArg(
      int argvIdx,
      void *buf,
      size_t remaining,
      error *err
   );

   void
   ParseBinaryArg(
      const char *arg,
      void *buf,
      size_t remaining,
      error *err
   );
};

typedef
std::function<void(CommandState&, error*)>
Command;

typedef
std::unordered_map<std::string, Command>
CommandList;

void
RegisterCommands(CommandList &list, error *err);

void
RegisterBpCommands(CommandList &list, error *err);

const char *
FormatAddr(
   CommandState &st,
   addr_t addr,
   char *buf,
   int len,
   error *err
);

void
Disassemble(
   CommandState &st,
   addr_t pc,
   int instrs,
   error *err
);

void
Disassemble(
   CommandState &st,
   int instrs,
   error *err
);

Command
DisassembleCommand();

Command
RegisterCommand();

} } // end namespace

#endif
