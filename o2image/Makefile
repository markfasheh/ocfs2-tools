TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

CFLAGS = $(OPTS) $(WARNINGS)

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

sbindir = $(root_sbindir)
SBIN_PROGRAMS = o2image

INCLUDES = -I$(TOPDIR)/include -I.
INCLUDES += $(GLIB_CFLAGS)
DEFINES = -DVERSION=\"$(VERSION)\"

MANS = o2image.8

CFILES = o2image.c

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) $(HFILES) o2image.8.in

o2image: $(OBJS) $(LIBOCFS2_DEPS)
	$(LINK) $(GLIB_LIBS) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
