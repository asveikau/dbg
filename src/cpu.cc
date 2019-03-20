/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#include <dbg/cpu.h>
#include <dbg/arch.h>

#include <common/c++/new.h>

#include <string.h>

int
dbg::Cpu::GetRegisterCount()
{
   return DBG_REGISTER_COUNT;
}

int
dbg::Cpu::GetRegisterSize(int regno)
{
   size_t size = 0;

#define SET_SIZE(regname) (size = sizeof(DBG_ACCESS_REG(regname)))
   DBG_EVAL_REGISTER(regno, SET_SIZE);
#undef SET_SIZE

   return size;
}

const char *
dbg::Cpu::GetRegisterName(int regno)
{
   const char *name = NULL;

#define STRINGIFY(x) (#x)
#define SET_NAME(regname) (name = STRINGIFY(regname))
   DBG_EVAL_REGISTER(regno, SET_NAME);
#undef SET_RETURN_VALUE

   return name;
}

int
dbg::Cpu::GetRegisterByName(const char *name)
{
   int i, n;

   // XXX
   // Slow linear search, but there's not likely to be a lot of registers,
   // so whatever.
   //
   for (i = 0, n=GetRegisterCount(); i<n; ++i)
   {
      const char *r = GetRegisterName(i);
      if (r && !strcmp(r, name))
         return i;
   }

   return -1;
}

void
dbg::Create(Cpu **p, error *err)
{
   common::Pointer<Cpu> r;
   New(r, err);
   ERROR_CHECK(err);
exit:
   if (ERROR_FAILED(err))
      r = nullptr;
   *p = r.Detach();
}