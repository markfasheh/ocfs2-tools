TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -O -ggdb
else
OPTS = -O2
endif

CFLAGS = $(OPTS) $(WARNINGS) -DOCFS2_FLAT_INCLUDES -DO2DLM_FLAT_INCLUDES

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

SBIN_PROGRAMS = mounted.ocfs2

INCLUDES = -I$(TOPDIR)/libocfs2/include -I$(TOPDIR)/libo2dlm/include

CFILES = mounted.c
OBJS = $(subst .c,.o,$(CFILES))

MANS = mounted.ocfs2.8

DIST_FILES = $(CFILES) mounted.ocfs2.8.in

mounted.ocfs2: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
