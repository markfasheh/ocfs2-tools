TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -ggdb -O0
else
OPTS = -O2
endif

CFLAGS = $(OPTS) $(WARNINGS) 

SBIN_PROGRAMS = mkfs.ocfs2

LIBOCFS2_CFLAGS = -I$(TOPDIR)/libocfs2/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

INCLUDES = $(LIBOCFS2_CFLAGS) -I. -I$(TOPDIR)/libo2dlm/include
DEFINES = -DOCFS2_FLAT_INCLUDES -DVERSION=\"$(VERSION)\" -DO2DLM_FLAT_INCLUDES

CFILES = mkfs.c check.c
OBJS = $(subst .c,.o,$(CFILES))

MANS = mkfs.ocfs2.8

DIST_FILES = $(CFILES) mkfs.ocfs2.8.in

mkfs.ocfs2: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
