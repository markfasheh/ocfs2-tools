TOPDIR = ..

include $(TOPDIR)/Preamble.make

sbindir = $(root_sbindir)
SBIN_PROGRAMS = mount.ocfs2

INCLUDES = -I$(TOPDIR)/include -Iinclude -I$(TOPDIR)/libocfs2/include \
	    -I$(TOPDIR)/libo2cb/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a
LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a
LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

ifdef OCFS2_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

CFLAGS := $(OPTS) -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

DEFINES = -DOCFS2_FLAT_INCLUDES \
		-DO2CB_FLAT_INCLUDES -DVERSION=\"$(VERSION)\"

CFILES = opts.c mount.ocfs2.c
CFILES += fstab.c mntent.c realpath.c sundries.c xmalloc.c

HFILES = $(subst .c,.h,$(CFILES))
HFILES += mount_constants.h nls.h paths.h

OBJS = $(subst .c,.o,$(CFILES))
MANS = mount.ocfs2.8

DIST_FILES = $(CFILES) $(HFILES) $(addsuffix .in,$(MANS))

all: mount.ocfs2

mount.ocfs2: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
