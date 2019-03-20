/*
 Copyright (C) 2019 Andrew Sveikauskas

 Permission to use, copy, modify, and distribute this software for any
 purpose with or without fee is hereby granted, provided that the above
 copyright notice and this permission notice appear in all copies.
*/

#ifndef dbg_misc_h_
#define dbg_misc_h_

#include <stddef.h>

namespace dbg
{

const char *
FormatSignal(char *buf, size_t sz, int sig);

} // end namespace

#endif