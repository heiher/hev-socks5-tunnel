# Makefile for hev-socks5-tunnel

PROJECT=hev-socks5-tunnel

CROSS_PREFIX :=
PP=$(CROSS_PREFIX)cpp
CC=$(CROSS_PREFIX)gcc
STRIP=$(CROSS_PREFIX)strip
CCFLAGS=-O3 -pipe -Wall -Werror $(CFLAGS) \
		-I$(SRCDIR)/misc \
		-I$(SRCDIR)/core/include  \
		-I$(THIRDPARTDIR)/yaml/include \
		-I$(THIRDPARTDIR)/lwip/include \
		-I$(THIRDPARTDIR)/lwip/include/ports/unix \
		-I$(THIRDPARTDIR)/hev-task-system/include
LDFLAGS=$(LFLAGS) \
		-L$(THIRDPARTDIR)/yaml/bin -lyaml \
		-L$(THIRDPARTDIR)/lwip/bin -llwip \
		-L$(THIRDPARTDIR)/hev-task-system/bin -lhev-task-system \
		-lpthread

SRCDIR=src
BINDIR=bin
CONFDIR=conf
BUILDDIR=build
INSTDIR=/usr/local
THIRDPARTDIR=third-part

CONFIG=$(CONFDIR)/main.yml
TARGET=$(BINDIR)/hev-socks5-tunnel
THIRDPARTS=$(THIRDPARTDIR)/yaml \
		   $(THIRDPARTDIR)/lwip \
		   $(THIRDPARTDIR)/hev-task-system

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

ENABLE_STATIC :=
ifeq ($(ENABLE_STATIC),1)
	CCFLAGS+=-static
	LDFLAGS+=-static
endif

V :=
ECHO_PREFIX := @
ifeq ($(V),1)
	undefine ECHO_PREFIX
endif

.PHONY: all clean install uninstall tp-build tp-clean

all : $(TARGET)

tp-build : $(THIRDPARTS)
	@$(foreach dir,$^,$(MAKE) --no-print-directory -C $(dir);)

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

$(INSTDIR)/bin/$(PROJECT) : $(TARGET)
	$(ECHO_PREFIX) install -d -m 0755 $(dir $@)
	$(ECHO_PREFIX) install -m 0755 $< $@
	@printf $(INSTMSG) $< $@

$(INSTDIR)/etc/$(PROJECT).yml : $(CONFIG)
	$(ECHO_PREFIX) install -d -m 0755 $(dir $@)
	$(ECHO_PREFIX) install -m 0644 $< $@
	@printf $(INSTMSG) $< $@

$(TARGET) : $(LDOBJS) tp-build
	$(ECHO_PREFIX) mkdir -p $(dir $@)
	$(ECHO_PREFIX) $(CC) -o $@ $(LDOBJS) $(LDFLAGS)
	@printf $(LINKMSG) $@
	$(ECHO_PREFIX) $(STRIP) $@
	@printf $(STRIPMSG) $@

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
