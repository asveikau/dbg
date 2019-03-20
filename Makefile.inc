
MAKEFILES_ROOT?=submodules/makefiles/
include $(MAKEFILES_ROOT)Makefile.inc
LIBCOMMON_ROOT?=submodules/common/
include $(LIBCOMMON_ROOT)Makefile.inc

LIBDBG_SRC := \
   $(LIBDBG_ROOT)src/breakpoint.cc \
   $(LIBDBG_ROOT)src/cpu.cc \
   $(LIBDBG_ROOT)src/dbg.cc \
   $(LIBDBG_ROOT)src/misc.cc \
   $(LIBDBG_ROOT)src/processevents.cc \
   $(LIBDBG_ROOT)src/shell/breakpoint.cc \
   $(LIBDBG_ROOT)src/shell/commands.cc \
   $(LIBDBG_ROOT)src/shell/disassemble.cc \
   $(LIBDBG_ROOT)src/shell/register.cc \
   $(LIBDBG_ROOT)src/shell/state.cc

ifneq (, $(filter $(shell uname -m),i386 i686 i86pc amd64 x86_64))

LIBDBG_SRC += \
   $(LIBDBG_ROOT)src/x86.cc

UDIS86_ROOT?=$(LIBDBG_ROOT)submodules/udis86/
PYTHON?=$(shell sh -c '(which python2; which python2.7 ; echo python) 2>/dev/null' | head -n1)

LIBDBG_CFLAGS+=-I$(UDIS86_ROOT)
LIBDBG_CFLAGS+=-DHAVE_STRING_H

# Hacky method to run udis86 generation at Makefile include time.
# We don't do this as a real target because that would induce races with
# make -j.
#
ifeq ($(NO_RECURSE)$(shell ls -d $(UDIS86_ROOT)libudis86 $(UDIS86_ROOT)libudis86/itab.c 2>/dev/null | wc -l | sed -e s/[' 	']//g), 1)
$(shell $(MAKE) NO_RECURSE=1 $(UDIS86_ROOT)libudis86/itab.c 1>&2)
endif

$(UDIS86_ROOT)libudis86/itab.h $(UDIS86_ROOT)libudis86/itab.c:
	(cd $(UDIS86_ROOT)libudis86; PYTHONPATH=../scripts $(PYTHON) ../scripts/ud_itab.py ../docs/x86/optable.xml .; cd -)

LIBDBG_SRC += \
   $(UDIS86_ROOT)libudis86/decode.c \
   $(UDIS86_ROOT)libudis86/syn-intel.c \
   $(UDIS86_ROOT)libudis86/itab.c \
   $(UDIS86_ROOT)libudis86/syn.c \
   $(UDIS86_ROOT)libudis86/syn-att.c \
   $(UDIS86_ROOT)libudis86/udis86.c

endif

ifneq (, $(filter $(PLATFORM),linux freebsd openbsd))
LIBDBG_SRC += \
   $(LIBDBG_ROOT)src/ptrace.cc
endif

ifeq ($(PLATFORM), darwin)

ifndef SDK_PREFIX
ifeq ($(shell ls -d /usr/include 2>/dev/null | wc -l), 0)
SDK_PREFIX:=/usr
else
SDKFLAGS:=$(shell xcodebuild -showsdks|grep \\-sdk\ macos|sed s/^.*-sdk/-sdk/|head -n1)
SDK_PREFIX:=$(shell xcodebuild $(SDKFLAGS) -find-library l | sed -e 's|/lib/libl.*$$||')
endif
endif

$(LIBDBG_ROOT)generated/mach_excServer.c:
	mkdir -p $(LIDBG_ROOT)generated
	(cd $(LIBDBG_ROOT)generated; mig $(SDK_PREFIX)/include/mach/mach_exc.defs; cd -)

LIBDBG_SRC += \
   $(LIBDBG_ROOT)generated/mach_excServer.o \
   $(LIBDBG_ROOT)src/darwin.cc
endif

LIBDBG_OBJS := $(shell $(SRC2OBJ) $(LIBDBG_SRC))

LIBDBG:=$(LIBDBG_ROOT)$(LIBPREFIX)dbg$(LIBSUFFIX)

$(LIBDBG): $(LIBDBG_OBJS)
	$(LIBWRAPPER) $(LIBDBG_OBJS)

LIBDBG_CFLAGS+=-Wall
LIBDBG_CXXFLAGS += -I$(LIBCOMMON_ROOT)include
LIBDBG_CXXFLAGS+=-I$(LIBDBG_ROOT)include
LIBDBG_CXXFLAGS+=${LIBDBG_CFLAGS}

-include $(LIBDBG_ROOT)depend.mk

