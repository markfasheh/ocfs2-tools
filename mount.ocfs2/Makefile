TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = mount.ocfs2

INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include \
	    -I$(TOPDIR)/libo2cb/include -I$(TOPDIR)/libo2dlm/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a
LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

ifdef OCFS_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

CFLAGS := $(OPTS) -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

DEFINES = -DOCFS2_FLAT_INCLUDES -DO2DLM_FLAT_INCLUDES -DVERSION=\"$(VERSION)\"

CFILES = opts.c mount.ocfs2.c
CFILES += fstab.c mntent.c realpath.c sundries.c xmalloc.c

HFILES = $(subst .c,.h,$(CFILES))
HFILES += mount_constants.h nls.h paths.h

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) $(HFILES)

mount.ocfs2: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(COM_ERR_LIBS)

group: group.o $(LIBOCFS2_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
