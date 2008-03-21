TOPDIR = ..

include $(TOPDIR)/Preamble.make

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm $(DL_LIBS)
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

sbindir = $(root_sbindir)
SBIN_PROGRAMS = tunefs.ocfs2

INCLUDES = -I$(TOPDIR)/include -I.
INCLUDES += $(GLIB_CFLAGS)
DEFINES = -DVERSION=\"$(VERSION)\"

MANS = tunefs.ocfs2.8

CFILES =		\
      tunefs.c		\
	query.c		\
	remove_slot.c	\
	sparse_file.c	\
	features.c	\
	resize.c	\
	format_slotmap.c

HFILES = tunefs.h

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) $(HFILES) tunefs.ocfs2.8.in

tunefs.ocfs2: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(GLIB_LIBS) $(LIBOCFS2_LIBS) $(UUID_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
