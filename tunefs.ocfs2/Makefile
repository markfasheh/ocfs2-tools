TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS2_DEBUG
OPTS = -O -ggdb
else
OPTS = -O2
endif

CFLAGS = $(OPTS) $(WARNINGS) 

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

sbindir = $(root_sbindir)
SBIN_PROGRAMS = tunefs.ocfs2

INCLUDES = -I$(TOPDIR)/include -I$(TOPDIR)/libocfs2/include -I.
INCLUDES += $(GLIB_CFLAGS)
DEFINES = -DOCFS2_FLAT_INCLUDES -DVERSION=\"$(VERSION)\"

MANS = tunefs.ocfs2.8

CFILES = tunefs.c query.c remove_slot.c sparse_file.c features.c
HFILES = tunefs.h

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) $(HFILES) tunefs.ocfs2.8.in

tunefs.ocfs2: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(GLIB_LIBS) $(LIBOCFS2_LIBS) $(UUID_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
