TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) $(WARNINGS) 

SBIN_PROGRAMS = mkfs.ocfs2 mkfs2.ocfs2

LIBOCFS2_CFLAGS = -I$(TOPDIR)/libocfs2/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2

INCLUDES = $(LIBOCFS2_CFLAGS) -I.
DEFINES = -DOCFS2_FLAT_INCLUDES -DVERSION=\"$(VERSION)\"

OPTIMIZE = -O2

CFLAGS += $(OPTIMIZE)

CFILES = mkfs.c
OBJS = $(subst .c,.o,$(CFILES))

MANS = mkfs.ocfs2.8

DIST_FILES = $(CFILES) mkfs.ocfs2.8.in

mkfs.ocfs2: $(OBJS)
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

mkfs2.ocfs2: mkfs2.o mkfs_utils.o
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

readdir: readdir.o
	$(LINK) 

fe: readdir.o
	$(LINK) 

include $(TOPDIR)/Postamble.make
