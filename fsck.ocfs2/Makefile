TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = fsck.ocfs2


INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2

CFLAGS := -g -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations -fno-strict-aliasing

DEFINES = -DOCFS2_FLAT_INCLUDES

ifeq ($(OCFS_PROCESSOR),x86_64)
  CFLAGS += -m64
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
  DEFINES += -D__ILP32__
endif

CFILES = fsck.c icount.c pass1.c problem.c util.c
HFILES = include/fsck.h include/icount.h include/pass1.h include/problem.h include/util.h

OBJS = $(subst .c,.o,$(CFILES))

VERSION_FILES = $(CFILES) $(HFILES)
VERSION_SRC = $(CFILES)
VERSION_PREFIX = OCFS2

DIST_FILES = $(CFILES) $(HFILES) 

DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

fsck.ocfs2: $(OBJS)
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
