TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) $(WARNINGS) -pthread

SBIN_PROGRAMS = ocfstool

MANS = ocfstool.8

DEFINES = -DOCFSTOOL_VERSION=\"$(VERSION)\"
INCLUDES = $(GTK_CFLAGS) -I$(TOPDIR)/ocfs/libocfs -I$(TOPDIR)/ocfs/debugocfs

OPTIMIZE = -O2

ifeq ($(OCFS_PROCESSOR),ppc64)
endif
ifeq ($(OCFS_PROCESSOR),x86_64)
  CFLAGS += -m64
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
endif

CFLAGS += $(OPTIMIZE)

CBITS = \
	ocfsbitmap.c	\
	ocfsbrowser.c	\
	ocfscellmap.c	\
	ocfsformat.c	\
	ocfsfreespace.c	\
	ocfsgenconfig.c	\
	ocfsgeneral.c	\
	ocfsguiutil.c	\
	ocfsmount.c	\
	ocfsnodemap.c	\
	ocfsplist.c	\
	ocfsprocess.c

HBITS = $(subst .c,.h,$(CBITS))

CFILES = $(CBITS) ocfstool.c
HFILES = $(HBITS)

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) $(HFILES) ocfstool.8.in

ocfstool: $(OBJS)
	$(LINK) $(GTK_LIBS) -L$(TOPDIR)/ocfs/debugocfs -ldebugocfs -L$(TOPDIR)/ocfs/libocfs -locfs

include $(TOPDIR)/Postamble.make
