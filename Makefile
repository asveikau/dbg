.PHONY: all all-phony clean depend
all: all-phony

CFLAGS=-O2 -g

include Makefile.inc

LDFLAGS += -L$(LIBCOMMON_ROOT) -lcommon
LDFLAGS += -lreadline

# OpenBSD libreadline depends on curses.
ifeq ($(PLATFORM), openbsd)
LDFLAGS += -lcurses
endif

all-phony: $(LIBDBG) dbg

ifeq ($(PLATFORM), darwin)
LDFLAGS += -sectcreate __TEXT __info_plist Info.plist
ifndef CODESIGN_IDENTITY
CODESIGN_IDENTITY:=$(shell security find-identity -v -p codesigning | head -n1 |cut -d ' ' -f 4)
endif
endif

dbg: $(LIBDBG) $(LIBCOMMON) $(LIBDBG_ROOT)src/shell/main.o
	$(CXX) -o $@ $(LIBDBG_ROOT)src/shell/main.o -L. -ldbg $(LDFLAGS)
	$(STRIP) $@
ifeq ($(PLATFORM), darwin)
ifeq ($(CODESIGN_IDENTITY), )
	@echo Darwin builds require a codesign identity
	@exit 1
endif
	codesign -s $(CODESIGN_IDENTITY) $@
endif

clean:
	rm -f $(LIBDBG) $(LIBDBG_OBJS)
	rm -f $(LIBCOMMON) $(LIBCOMMON_OBJS)
	rm -rf dbg *.debug *.dSYM $(LIBDBG_ROOT)src/shell/main.o
	rm -rf generated
	(cd submodules/udis86; git clean -dfx; cd -)

export
depend:
	env PROJECT=LIBDBG $(DEPEND) \
	src/*.cc src/shell/*.cc submodules/udis86/libudis86/*.c \
		> depend.mk
