TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS) 

SBIN_PROGRAMS = fsck.ocfs2

INCLUDES = -I$(TOPDIR)/libocfs2/include

OPTIMIZE = -O2

ifeq ($(OCFS_PROCESSOR),x86_64)
  CFLAGS += -m64
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
  DEFINES += -D__ILP32__
endif

CFLAGS += $(OPTIMIZE)

VERSION_FILES = fsck.c 
VERSION_SRC = fsck.c
VERSION_PREFIX = OCFS2

#MANS = fsck.ocfs2.8

DIST_FILES = $(VERSION_FILES) $(VERSION_SRC) #mkfs.ocfs2.8.in

fsck.ocfs2: fsck.o
	$(LINK) 

include $(TOPDIR)/Postamble.make
