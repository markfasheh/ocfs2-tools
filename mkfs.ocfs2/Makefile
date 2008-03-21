TOPDIR = ..

include $(TOPDIR)/Preamble.make

sbindir = $(root_sbindir)
SBIN_PROGRAMS = mkfs.ocfs2

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm $(DL_LIBS)
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

INCLUDES = -I$(TOPDIR)/include -I.
DEFINES = -DVERSION=\"$(VERSION)\"

CFILES = mkfs.c check.c
HFILES = mkfs.h

OBJS = $(subst .c,.o,$(CFILES))

MANS = mkfs.ocfs2.8

DIST_FILES = $(CFILES) $(HFILES) mkfs.ocfs2.8.in

mkfs.ocfs2: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS) $(UUID_LIBS)

include $(TOPDIR)/Postamble.make
