TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall

ifdef OCFS2_DEBUG
OPTS = -g
endif

LIBOCFS2_CFLAGS = -I$(TOPDIR)/libocfs2/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a 

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_CFLAGS = -I$(TOPDIR)/libo2cb/include
LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

CFLAGS = $(OPTS) $(WARNINGS) 

sbindir = $(root_sbindir)
SBIN_PROGRAMS = ocfs2cdsl

DEFINES = -DVERSION=\"$(VERSION)\"
DEFINES += -DOCFS2_FLAT_INCLUDES -DO2CB_FLAT_INCLUDES
DEFINES += -DG_DISABLE_DEPRECATED

INCLUDES = -I$(TOPDIR)/include $(LIBOCFS2_CFLAGS) $(LIBO2CB_CFLAGS) $(GLIB_CFLAGS)

OPTIMIZE = -O2

CFLAGS += $(OPTIMIZE)

CFILES = ocfs2cdsl.c
OBJS = $(subst .c,.o,$(CFILES))

DIST_RULES = dist-incdir

MANS = ocfs2cdsl.8

DIST_FILES = $(CFILES) ocfs2cdsl.8.in

ocfs2cdsl: $(OBJS)
	$(LINK) $(GLIB_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
