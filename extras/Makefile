TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS) 

UNINST_PROGRAMS = find_hardlinks find_dup_extents

INCLUDES = -I../libocfs2/include

OPTIMIZE = -O2

ifeq ($(OCFS_PROCESSOR),x86_64)
  CFLAGS += -m64
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
  DEFINES += -D__ILP32__
endif

DEFINES += -DOCFS2_FLAT_INCLUDES

CFLAGS += $(OPTIMIZE)

FIND_HARDLINKS_CFILES = find_hardlinks.c
FIND_DUP_EXTENTS_CFILES = find_dup_extents.c

DIST_FILES = $(FIND_HARDLINKS_CFILES) $(FIND_DUP_EXTENTS_CFILES)
FIND_HARDLINKS_OBJS = $(subst .c,.o,$(FIND_HARDLINKS_CFILES))
FIND_DUP_EXTENTS_OBJS = $(subst .c,.o,$(FIND_DUP_EXTENTS_CFILES))

LIBOCFS2 = ../libocfs2/libocfs2.a
LIBS = $(LIBOCFS2) $(COM_ERR_LIBS)

find_hardlinks: $(FIND_HARDLINKS_OBJS) $(LIBS)
	$(LINK) 

find_dup_extents: $(FIND_DUP_EXTENTS_OBJS) $(LIBS)
	$(LINK) 

include $(TOPDIR)/Postamble.make
