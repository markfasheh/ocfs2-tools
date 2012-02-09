TOPDIR = ..

include $(TOPDIR)/Preamble.make

sbindir = $(root_sbindir)
SBIN_PROGRAMS = mount.ocfs2

INCLUDES = -I$(TOPDIR)/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a
LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm $(DL_LIBS)
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a
LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

DEFINES = -DVERSION=\"$(VERSION)\"

CORE_CFILES = fstab.c mntent.c realpath.c sundries.c xmalloc.c opts.c
MOUNT_CFILES = mount.ocfs2.c

HFILES = $(subst .c,.h,$(MOUNT_CFILES) $(CORE_CFILES))
HFILES += mount_constants.h nls.h paths.h

MOUNT_OBJS = $(subst .c,.o,$(MOUNT_CFILES) $(CORE_CFILES))
MANS = mount.ocfs2.8

DIST_FILES = $(CORE_CFILES) $(MOUNT_CFILES)	\
	     $(HFILES) $(addsuffix .in,$(MANS))

mount.ocfs2: $(MOUNT_OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS) $(AIO_LIBS)

include $(TOPDIR)/Postamble.make
