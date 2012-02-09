TOPDIR = ..

include $(TOPDIR)/Preamble.make

DEFINES = -DG_DISABLE_DEPRECATED -DLINUX
DEFINES += -DVERSION=\"$(VERSION)\"

INCLUDES = -I$(TOPDIR)/include -Iinclude
INCLUDES += $(GLIB_CFLAGS)

UNINST_PROGRAMS = fswreck

CFILES = main.c corrupt.c chain.c extent.c group.c inode.c local_alloc.c truncate_log.c special.c symlink.c dir.c journal.c quota.c refcount.c discontig_bg.c

HFILES = 			\
	include/chain.h		\
	include/dir.h		\
	include/fsck_type.h	\
	include/inode.h		\
	include/local_alloc.h	\
	include/special.h	\
	include/truncate_log.h	\
	include/corrupt.h	\
	include/extent.h	\
	include/group.h		\
	include/journal.h	\
	include/main.h		\
	include/symlink.h	\
	include/quota.h		\
	include/refcount.h	\
	include/discontig_bg.h

DIST_FILES = $(CFILES) $(HFILES)
DIST_RULES = dist-subdircreate

OBJS = $(subst .c,.o,$(CFILES))

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm $(DL_LIBS)
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

fswreck: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(GLIB_LIBS) $(COM_ERR_LIBS) $(AIO_LIBS)

include $(TOPDIR)/Postamble.make
