TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS) 

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2

SBIN_PROGRAMS = mounted.ocfs2

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

CFILES = mounted.c
OBJS = $(subst .c,.o,$(CFILES))

#MANS = fsck.ocfs2.8

DIST_FILES = $(CFILES)

mounted.ocfs2: $(OBJS)
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
