TOPDIR = ../..

include $(TOPDIR)/Preamble.make

DEFINES=-DMODULE -DLINUX -D__KERNEL__
CFLAGS=-I../../libocfs/Common/inc -I$(INC) -I../../libocfs/Linux/inc
LDADD=-nostdlib

OPTIMIZE = -O2

ifeq ($(OCFS_PROCESSOR),x86_64)
  CFLAGS += -m64
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
endif

ifdef OCFS_DEBUG
  CFLAGS += -g
  DEFINES += -DDEBUG
endif

CFLAGS += $(DEFINES) -w

DIST_RULES = dist-incdir

VERSION_FILES = diskstructs.c netstructs.c
VERSION_SRC = diskstructs.c netstructs.c
VERSION_PREFIX = OCFS

BIN_PROGRAMS = diskstructs netstructs sizetest hexsizetest

all: diskstructs netstructs sizetest hexsizetest

diskstructs: diskstructs.c
	$(LINK)

netstructs: netstructs.c
	$(LINK)

sizetest: sizetest.c
	$(LINK)

hexsizetest: sizetest.c
	$(LINK) -DUSE_HEX

dist-incdir:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/inc

include $(TOPDIR)/Postamble.make
