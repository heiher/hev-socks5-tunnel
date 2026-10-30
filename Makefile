# Makefile for hev-socks5-tunnel

# Native Windows build with clang-cl + MSVC lib/link:
#   make [MSVC=1] [ARCH=x64|x86|arm64] [DEBUG=1] [STATIC_CRT=1] [all|exec|static|shared|dist|clean]
# Run it from a Visual Studio developer environment, which provides
# INCLUDE/LIB/PATH for the target ARCH. The bundled third-part libraries are
# compiled directly here instead of via their own (GNU only) Makefiles.
# MSVC is picked automatically on native Windows (a Visual Studio developer
# prompt or plain cmd/PowerShell) and left off under MSYS2/MinGW, which set
# MSYSTEM. Force it either way with MSVC=1 or MSVC=0.
MSVC ?=
ifeq ($(MSVC),)
ifeq ($(OS),Windows_NT)
ifeq ($(MSYSTEM),)
MSVC=1
endif
endif
endif
ifeq ($(MSVC),1)

PROJECT=hev-socks5-tunnel

CC=clang-cl
AR=lib.exe
LINK=link.exe

ARCH :=
ifeq ($(ARCH),)
	ARCH=x64
endif

ifeq ($(ARCH),x64)
	CLANG_TARGET=x86_64-pc-windows-msvc
	MACHINE=X64
	WINTUN_DLL=wintun.dll
	CET_OPT=-CETCOMPAT:NO
	OUTDIR=build
else ifeq ($(ARCH),x86)
	CLANG_TARGET=i686-pc-windows-msvc
	MACHINE=X86
	WINTUN_DLL=wintun-x86.dll
	CET_OPT=-CETCOMPAT:NO
	OUTDIR=build/x86
else ifeq ($(ARCH),arm64)
	CLANG_TARGET=aarch64-pc-windows-msvc
	MACHINE=ARM64
	WINTUN_DLL=wintun-arm64.dll
	CET_OPT=
	OUTDIR=build/arm64
else
	$(error Unknown ARCH "$(ARCH)", use x64, x86 or arm64)
endif

SRCDIR=src
THIRDPARTDIR=third-part
TASKDIR=$(THIRDPARTDIR)/hev-task-system
OBJDIR=$(OUTDIR)/obj
DISTDIR=$(OUTDIR)/dist

ENABLE_DEBUG :=
DEBUG :=
STATIC_CRT :=
ifneq (,$(filter 1,$(ENABLE_DEBUG) $(DEBUG)))
	CRT_FLAGS=-Od -Zi
	CRT_SUFFIX=d
else
	CRT_FLAGS=-O2
	CRT_SUFFIX=
endif
ifeq ($(STATIC_CRT),1)
	CRT_FLAGS+=-MT$(CRT_SUFFIX)
else
	CRT_FLAGS+=-MD$(CRT_SUFFIX)
endif

-include build.mk

# '-' option prefixes (instead of '/') keep MSYS from rewriting them as paths.
CCFLAGS=-nologo -c -W4 -utf-8 -std:c11 -Xclang -std=gnu11 $(CRT_FLAGS) \
	--target=$(CLANG_TARGET) -Wno-unused-command-line-argument \
	-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_WARNINGS \
	-DHAVE_CONFIG_H=0 -DYAML_DECLARE_STATIC \
	-DYAML_VERSION_MAJOR=0 -DYAML_VERSION_MINOR=2 -DYAML_VERSION_PATCH=5 \
	-DYAML_VERSION_STRING=\"0.2.5\" -DSSIZE_MAX=INT_MAX -DLWIP_NO_UNISTD_H=1 \
	-DENABLE_MEMALLOC_SLICE -DENABLE_STACK_OVERFLOW_DETECTION \
	-DCONFIG_STACK_BACKEND=STACK_HEAP -DCONFIG_STACK_OVERFLOW_DETECTION=1 \
	-DCONFIG_MEMALLOC_SLICE_ALIGN=16 -DCONFIG_MEMALLOC_SLICE_MAX_SIZE=4096 \
	-DCONFIG_MEMALLOC_SLICE_MAX_COUNT=1000 -DCONFIG_SCHED_CLOCK=CLOCK_NONE -DCONFIG_SCHED_IO_POLL_INTERVAL=50000 \
	-DEXPORT_SYMBOL= -D_WINSOCK_DEPRECATED_NO_WARNINGS \
	-Wno-microsoft-enum-forward-reference -Wno-pragma-pack \
	-Dstrcasecmp=_stricmp -Dstrncasecmp=_strnicmp \
	$(VERSION_CFLAGS) \
	-I$(SRCDIR) -I$(SRCDIR)/misc -I$(SRCDIR)/core/include \
	-I$(THIRDPARTDIR)/yaml/include \
	-I$(THIRDPARTDIR)/lwip/src/include \
	-I$(THIRDPARTDIR)/lwip/src/ports/include \
	-I$(TASKDIR)/include -I$(TASKDIR)/src \
	-I$(THIRDPARTDIR)/wintun/include \
	$(CFLAGS)
LIB_CCFLAGS=$(CCFLAGS) -DENABLE_LIBRARY
EXE_CCFLAGS=$(CCFLAGS)

WINLIBS=Ws2_32.lib Iphlpapi.lib Advapi32.lib
LINKFLAGS=-NOLOGO $(CET_OPT) -MACHINE:$(MACHINE) $(LFLAGS)
EXPORTS=-EXPORT:hev_socks5_tunnel_main \
	-EXPORT:hev_socks5_tunnel_main_from_file \
	-EXPORT:hev_socks5_tunnel_main_from_str \
	-EXPORT:hev_socks5_tunnel_quit \
	-EXPORT:hev_socks5_tunnel_stats

# Sources shared by the library and the executable.
rwildcard=$(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) \
	$(filter $(subst *,%,$2),$d))

# hev-task-system sources are guarded per platform, so the whole tree is built.
COMMON_SRCS=$(wildcard $(THIRDPARTDIR)/yaml/src/*.c) \
	$(wildcard $(THIRDPARTDIR)/lwip/src/core/*.c) \
	$(wildcard $(THIRDPARTDIR)/lwip/src/core/ipv4/*.c) \
	$(wildcard $(THIRDPARTDIR)/lwip/src/core/ipv6/*.c) \
	$(call rwildcard,$(TASKDIR)/src/,*.c *.S) \
	$(filter-out $(SRCDIR)/core/src/hev-rbtree.c,$(wildcard $(SRCDIR)/core/src/*.c))

# Sources that differ between the library (ENABLE_LIBRARY) and the executable.
TUNNEL_SRCS=$(addprefix $(SRCDIR)/, \
	hev-config.c hev-main.c hev-mapped-dns.c hev-socks5-session.c \
	hev-socks5-session-tcp.c hev-socks5-session-udp.c hev-socks5-tunnel.c \
	hev-tunnel-windows.c misc/hev-exec.c misc/hev-logger.c \
	misc/hev-ring-buffer.c misc/hev-utils.c misc/hev-wintun.c) \
	$(THIRDPARTDIR)/lwip/src/ports/win32/lib/sys_arch.c

toobj=$(addsuffix .obj,$(basename $1))
COMMON_OBJS=$(addprefix $(OBJDIR)/,$(call toobj,$(COMMON_SRCS)))
LIB_OBJS=$(COMMON_OBJS) $(addprefix $(OBJDIR)/lib/,$(call toobj,$(TUNNEL_SRCS)))
EXE_OBJS=$(COMMON_OBJS) $(addprefix $(OBJDIR)/exe/,$(call toobj,$(TUNNEL_SRCS)))
DEPEND=$(LIB_OBJS:.obj=.d) $(EXE_OBJS:.obj=.d)

EXEC_TARGET=$(DISTDIR)/bin/$(PROJECT).exe
STATIC_TARGET=$(DISTDIR)/lib/$(PROJECT)-static.lib
SHARED_TARGET=$(DISTDIR)/bin/$(PROJECT).dll
IMPORT_TARGET=$(DISTDIR)/lib/$(PROJECT).lib

BUILDMSG="\e[1;31mBUILD\e[0m %s\n"
LINKMSG="\e[1;34mLINK\e[0m  \e[1;32m%s\e[0m\n"
CLEANMSG="\e[1;34mCLEAN\e[0m %s\n"

V :=
ECHO_PREFIX := @
ifeq ($(V),1)
	undefine ECHO_PREFIX
endif

.DEFAULT_GOAL := all
.PHONY: all exec static shared dist clean

all : static shared exec dist

exec : $(EXEC_TARGET)

static : $(STATIC_TARGET)

shared : $(SHARED_TARGET)

dist : $(EXEC_TARGET) $(STATIC_TARGET) $(SHARED_TARGET)
	$(ECHO_PREFIX) mkdir -p $(DISTDIR)/include
	$(ECHO_PREFIX) cp $(SRCDIR)/hev-main.h $(DISTDIR)/include/
	$(ECHO_PREFIX) cp $(THIRDPARTDIR)/wintun/bin/$(WINTUN_DLL) $(DISTDIR)/bin/wintun.dll

clean :
	$(ECHO_PREFIX) $(RM) -rf $(OUTDIR)
	@printf $(CLEANMSG) $(PROJECT)

$(EXEC_TARGET) : $(EXE_OBJS)
	$(ECHO_PREFIX) mkdir -p $(dir $@)
	$(ECHO_PREFIX) $(LINK) $(LINKFLAGS) -SUBSYSTEM:CONSOLE -OUT:$@ $^ $(WINLIBS)
	@printf $(LINKMSG) $@

$(STATIC_TARGET) : $(LIB_OBJS)
	$(ECHO_PREFIX) mkdir -p $(dir $@)
	$(ECHO_PREFIX) $(AR) -NOLOGO -MACHINE:$(MACHINE) -OUT:$@ $^
	@printf $(LINKMSG) $@

$(SHARED_TARGET) : $(LIB_OBJS)
	$(ECHO_PREFIX) mkdir -p $(dir $@) $(dir $(IMPORT_TARGET))
	$(ECHO_PREFIX) $(LINK) $(LINKFLAGS) -DLL -OUT:$@ -IMPLIB:$(IMPORT_TARGET) \
		$^ $(WINLIBS) $(EXPORTS)
	@printf $(LINKMSG) $@

$(OBJDIR)/lib/%.obj : %.c
	$(ECHO_PREFIX) mkdir -p $(dir $@)
	$(ECHO_PREFIX) $(CC) $(LIB_CCFLAGS) -clang:-MMD -clang:-MF$(@:.obj=.d) -Fo$@ $<
	@printf $(BUILDMSG) $<

$(OBJDIR)/exe/%.obj : %.c
	$(ECHO_PREFIX) mkdir -p $(dir $@)
	$(ECHO_PREFIX) $(CC) $(EXE_CCFLAGS) -clang:-MMD -clang:-MF$(@:.obj=.d) -Fo$@ $<
	@printf $(BUILDMSG) $<

$(OBJDIR)/%.obj : %.c
	$(ECHO_PREFIX) mkdir -p $(dir $@)
	$(ECHO_PREFIX) $(CC) $(LIB_CCFLAGS) -clang:-MMD -clang:-MF$(@:.obj=.d) -Fo$@ $<
	@printf $(BUILDMSG) $<

# GAS assembly (hev-task-execute.S) is also built by clang-cl.
$(OBJDIR)/%.obj : %.S
	$(ECHO_PREFIX) mkdir -p $(dir $@)
	$(ECHO_PREFIX) $(CC) -nologo -c --target=$(CLANG_TARGET) -I$(TASKDIR)/src -Fo$@ $<
	@printf $(BUILDMSG) $<

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPEND)
endif

else

PROJECT=hev-socks5-tunnel

CROSS_PREFIX :=
PP=$(CROSS_PREFIX)cpp
CC=$(CROSS_PREFIX)gcc
AR=$(CROSS_PREFIX)ar
STRIP=$(CROSS_PREFIX)strip
CCFLAGS=-O3 -pipe -Wall -Werror $(CFLAGS) \
		-I$(SRCDIR) \
		-I$(SRCDIR)/misc \
		-I$(SRCDIR)/core/include  \
		-I$(THIRDPARTDIR)/yaml/include \
		-I$(THIRDPARTDIR)/wintun/include \
		-I$(THIRDPARTDIR)/lwip/src/include \
		-I$(THIRDPARTDIR)/lwip/src/ports/include \
		-I$(THIRDPARTDIR)/hev-task-system/include
LDFLAGS=-L$(THIRDPARTDIR)/yaml/bin -lyaml \
		-L$(THIRDPARTDIR)/lwip/bin -llwip \
		-L$(THIRDPARTDIR)/hev-task-system/bin -lhev-task-system \
		-lpthread $(LFLAGS)

CCFLAGS+=$(shell $(CC) -Werror -fno-stack-clash-protection -E -x c /dev/null \
           >/dev/null 2>&1 && echo -fno-stack-clash-protection)

SRCDIR=src
BINDIR=bin
CONFDIR=conf
BUILDDIR=build
INSTDIR=/usr/local
THIRDPARTDIR=third-part

CONFIG=$(CONFDIR)/main.yml
EXEC_TARGET=$(BINDIR)/hev-socks5-tunnel
STATIC_TARGET=$(BINDIR)/lib$(PROJECT).a
SHARED_TARGET=$(BINDIR)/lib$(PROJECT).so
THIRDPARTS=$(THIRDPARTDIR)/yaml \
		   $(THIRDPARTDIR)/lwip \
		   $(THIRDPARTDIR)/hev-task-system

$(STATIC_TARGET) : CCFLAGS+=-DENABLE_LIBRARY
$(SHARED_TARGET) : CCFLAGS+=-DENABLE_LIBRARY -fPIC
$(SHARED_TARGET) : LDFLAGS+=-shared -pthread

-include build.mk
CCFLAGS+=$(VERSION_CFLAGS)
TPFLAGS=ENABLE_MEMALLOC_SLICE=0 ENABLE_STACK_OVERFLOW_DETECTOR=1
CCSRCS=$(filter %.c,$(SRCFILES))
ASSRCS=$(filter %.S,$(SRCFILES))
LDOBJS=$(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(CCSRCS)) \
	   $(patsubst $(SRCDIR)/%.S,$(BUILDDIR)/%.o,$(ASSRCS))
DEPEND=$(LDOBJS:.o=.dep)

BUILDMSG="\e[1;31mBUILD\e[0m %s\n"
LINKMSG="\e[1;34mLINK\e[0m  \e[1;32m%s\e[0m\n"
STRIPMSG="\e[1;34mSTRIP\e[0m \e[1;32m%s\e[0m\n"
CLEANMSG="\e[1;34mCLEAN\e[0m %s\n"
INSTMSG="\e[1;34mINST\e[0m  %s -> %s\n"
UNINSMSG="\e[1;34mUNINS\e[0m %s\n"

ENABLE_DEBUG :=
ifeq ($(ENABLE_DEBUG),1)
	CCFLAGS+=-g -O0 -DENABLE_DEBUG
	STRIP=true
endif

ENABLE_STATIC :=
ifeq ($(ENABLE_STATIC),1)
	CCFLAGS+=-static
endif

ifeq ($(MSYSTEM),MSYS)
	LDFLAGS+=-lmsys-2.0 -lws2_32 -lIphlpapi
endif

V :=
ECHO_PREFIX := @
ifeq ($(V),1)
	undefine ECHO_PREFIX
endif

.PHONY: exec static shared clean install uninstall tp-static tp-shared tp-clean

exec : $(EXEC_TARGET)

static : $(STATIC_TARGET)

shared : $(SHARED_TARGET)

tp-static : $(THIRDPARTS)
	@$(foreach dir,$^,$(MAKE) --no-print-directory -C $(dir) $(TPFLAGS) static;)

tp-shared : $(THIRDPARTS)
	@$(foreach dir,$^,$(MAKE) --no-print-directory -C $(dir) $(TPFLAGS) shared;)

tp-clean : $(THIRDPARTS)
	@$(foreach dir,$^,$(MAKE) --no-print-directory -C $(dir) clean;)

clean : tp-clean
	$(ECHO_PREFIX) $(RM) -rf $(BINDIR) $(BUILDDIR)
	@printf $(CLEANMSG) $(PROJECT)

install : $(INSTDIR)/bin/$(PROJECT) $(INSTDIR)/etc/$(PROJECT).yml

uninstall :
	$(ECHO_PREFIX) $(RM) -rf $(INSTDIR)/bin/$(PROJECT)
	@printf $(UNINSMSG) $(INSTDIR)/bin/$(PROJECT)
	$(ECHO_PREFIX) $(RM) -rf $(INSTDIR)/etc/$(PROJECT).yml
	@printf $(UNINSMSG) $(INSTDIR)/etc/$(PROJECT).yml

$(INSTDIR)/bin/$(PROJECT) : $(EXEC_TARGET)
	$(ECHO_PREFIX) install -d -m 0755 $(dir $@)
	$(ECHO_PREFIX) install -m 0755 $< $@
	@printf $(INSTMSG) $< $@

$(INSTDIR)/etc/$(PROJECT).yml : $(CONFIG)
	$(ECHO_PREFIX) install -d -m 0755 $(dir $@)
	$(ECHO_PREFIX) install -m 0644 $< $@
	@printf $(INSTMSG) $< $@

$(EXEC_TARGET) : $(LDOBJS) tp-static
	$(ECHO_PREFIX) mkdir -p $(dir $@)
	$(ECHO_PREFIX) $(CC) $(CCFLAGS) -o $@ $(LDOBJS) $(LDFLAGS)
	@printf $(LINKMSG) $@
	$(ECHO_PREFIX) $(STRIP) $@
	@printf $(STRIPMSG) $@

$(STATIC_TARGET) : $(LDOBJS) tp-static
	$(ECHO_PREFIX) mkdir -p $(dir $@)
	$(ECHO_PREFIX) $(AR) csq $@ $(LDOBJS)
	@printf $(LINKMSG) $@

$(SHARED_TARGET) : $(LDOBJS) tp-shared
	$(ECHO_PREFIX) mkdir -p $(dir $@)
	$(ECHO_PREFIX) $(CC) $(CCFLAGS) -o $@ $(LDOBJS) $(LDFLAGS)
	@printf $(LINKMSG) $@

$(BUILDDIR)/%.dep : $(SRCDIR)/%.c
	$(ECHO_PREFIX) mkdir -p $(dir $@)
	$(ECHO_PREFIX) $(PP) $(CCFLAGS) -MM -MT$(@:.dep=.o) -MF$@ $< 2>/dev/null

$(BUILDDIR)/%.o : $(SRCDIR)/%.c
	$(ECHO_PREFIX) mkdir -p $(dir $@)
	$(ECHO_PREFIX) $(CC) $(CCFLAGS) -c -o $@ $<
	@printf $(BUILDMSG) $<

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPEND)
endif

endif
