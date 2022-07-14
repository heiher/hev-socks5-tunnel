# Build

rwildcard=$(foreach d,$(wildcard $1*), \
          $(call rwildcard,$d/,$2) \
          $(filter $(subst *,%,$2),$d))

SRCFILES=$(call rwildcard,$(SRCDIR)/,*.c *.S)
VERSION_CFLAGS=-DCOMMIT_ID=\"$(shell git -C $(SRCDIR) rev-parse --short HEAD)\"
