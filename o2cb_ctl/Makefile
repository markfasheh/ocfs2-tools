TOPDIR = ..

include $(TOPDIR)/Preamble.make

sbindir = $(root_sbindir)
SBIN_PROGRAMS = o2cb_ctl o2cb_hb_config

INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include \
	    -I$(TOPDIR)/libo2cb/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2CB_LIBS  = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

ifdef OCFS2_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

ifndef OCFS2_DYNAMIC_CTL
LDFLAGS += -static
endif

CFLAGS := $(OPTS) -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

DEFINES = -DO2CB_FLAT_INCLUDES -DVERSION=\"$(VERSION)\"

JCONFIG_CFILES = jconfig.c jiterator.c
JCONFIG_OBJS = $(subst .c,.o,$(JCONFIG_CFILES))

O2CB_CTL_CFILES = o2cb_ctl.c o2cb_config.c
O2CB_CTL_OBJS = $(subst .c,.o,$(O2CB_CTL_CFILES)) $(JCONFIG_OBJS)

O2CB_HB_CONFIG_CFILES = o2cb_hb_config.c
O2CB_HB_CONFIG_OBJS =				\
	$(subst .c,.o,$(O2CB_HB_CONFIG_CFILES))	\
	$(JCONFIG_OBJS)


HEADERS = o2cb_config.h jconfig.h jiterator.h

CFILES = $(O2CB_CTL_CFILES) $(O2CB_HB_CONFIG_CFILES) $(JCONFIG_CFILES)

MANS = o2cb_ctl.8

DIST_FILES = $(CFILES) $(HEADERS) o2cb_ctl.8.in

DIST_RULES = dist-subdircreate

jconfig_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED
jiterator_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED
o2cb_config_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED
o2cb_ctl_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED
o2cb_hb_config_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED

o2cb_ctl: $(O2CB_CTL_OBJS) $(LIBOCFS2_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBO2CB_LIBS) $(GLIB_LIBS) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

o2cb_hb_config: $(O2CB_HB_CONFIG_OBJS)
	$(LINK) $(GLIB_LIBS)

include $(TOPDIR)/Postamble.make
