TOPDIR = ..

include $(TOPDIR)/Preamble.make

sbindir = $(root_sbindir)
SBIN_PROGRAMS = o2cb_ctl

INCLUDES = -I$(TOPDIR)/include

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2CB_LIBS  = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

ifndef OCFS2_DYNAMIC_CTL
LDFLAGS += -static
endif

DEFINES = -DVERSION=\"$(VERSION)\"

O2CB_CTL_CFILES = o2cb_ctl.c o2cb_config.c \
	jconfig.c jiterator.c

O2CB_CTL_OBJS = $(subst .c,.o,$(O2CB_CTL_CFILES))

HFILES = o2cb_config.h jconfig.h jiterator.h

CFILES = $(CLUSTERBO_CFILES) $(O2CB_CTL_CFILES)

MANS = o2cb_ctl.8

DIST_FILES = $(CFILES) $(HFILES) o2cb_ctl.8.in

DIST_RULES = dist-subdircreate

jconfig_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED
jiterator_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED
o2cb_config_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED
o2cb_ctl_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED

o2cb_ctl: $(O2CB_CTL_OBJS) $(LIBOCFS2_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBO2CB_LIBS) $(GLIB_LIBS) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
