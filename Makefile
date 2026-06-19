# Makefile for hev-socks5-tunnel

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
	@$(foreach dir,$^,$(MAKE) --no-print-directory -C $(dir) static;)

tp-shared : $(THIRDPARTS)
	@$(foreach dir,$^,$(MAKE) --no-print-directory -C $(dir) shared;)

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
