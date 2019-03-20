/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef dbg_types_h_
#define dbg_types_h_

#include <stddef.h>
#include <inttypes.h>

#include <common/c++/refcount.h>
#include <common/error.h>

namespace dbg
{

typedef uintptr_t addr_t;

struct Debugger;
struct Cpu;
struct Process;

} // end namespace

#endif
