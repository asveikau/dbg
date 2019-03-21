# dbg

A toy debugger.  Just started out goofing around with ptrace(2) and this came
out the other end.

Currently works on:

* CPUs: x86/amd64 only at the moment.

* Operating Systems: Linux, FreeBSD, OpenBSD, macOS.

Commands are inspired by windbg.  Currently:

* bp - Create breakpoint

* bl - List breakpoints

* bc - Clear breakpoint

* db, dw, dd, dq - Dump memory in 8, 16, 32, and 64 bit quantities respectively

* eb, ew, ed, eq - Edit memory in the same units.

* .detach - Detach the target

* t - Single instruction step

* g - Go (continue execution)

* k - Stack trace

* r - Print or edit registers

* q - Quit the process & debugger 

# TODO

* Symbolication, DWARF, etc.
* Threads.
* More CPU arches and operating systems:
    - Windows?  The APIs are pretty clean.
    - ARM?  RISC-V?
    - Dead platforms?  (Solaris, SPARC, PowerPC?)

# Building

    $ git submodule update --init
    $ make   # (or gmake on some systems, eg. *BSD)

For x86 you will need python2 on the path.

For macOS you will need a code sign identity, as Apple does not allow unsigned
apps to attach for debugging.

The app requires libreadline.
