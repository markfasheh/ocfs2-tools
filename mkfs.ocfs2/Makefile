TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS) 

SBIN_PROGRAMS = mkfs.ocfs2

LIBOCFS2_CFLAGS = -I$(TOPDIR)/libocfs2/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2

INCLUDES = $(LIBOCFS2_CFLAGS)
DEFINES = -DOCFS2_FLAT_INCLUDES -DVERSION=\"$(VERSION)\"

OPTIMIZE = -O2

ifeq ($(OCFS_PROCESSOR),x86_64)
  CFLAGS += -m64
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
  DEFINES += -D__ILP32__
endif
ifeq ($(OCFS_PROCESSOR),s390x)
  DEFINES += -D__LP64__
endif

CFLAGS += $(OPTIMIZE)

CFILES = mkfs.c
OBJS = $(subst .c,.o,$(CFILES))

MANS = mkfs.ocfs2.8

DIST_FILES = $(CFILES) mkfs.ocfs2.8.in

mkfs.ocfs2: $(OBJS)
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

readdir: readdir.o
	$(LINK) 

fe: readdir.o
	$(LINK) 

include $(TOPDIR)/Postamble.make
