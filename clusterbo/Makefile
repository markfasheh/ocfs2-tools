TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = clusterbo o2cb_ctl
LIBRARIES = libo2cb.a

INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include \
	    -I$(TOPDIR)/libocfs2cluster/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2CB_LIBS  = -L. -lo2cb
LIBO2CB_DEPS = libo2cb.a

ifdef OCFS_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

CFLAGS := $(OPTS) -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

DEFINES = -DOCFS2_FLAT_INCLUDES -DVERSION=\"$(VERSION)\"

LIBO2CB_CFILES = o2cb_abi.c
LIBO2CB_OBJS = $(subst .c,.o,$(LIBO2CB_CFILES))

CLUSTERBO_CFILES = clusterbo.c
CLUSTERBO_OBJS = $(subst .c,.o,$(CLUSTERBO_CFILES))

O2CB_CTL_CFILES = o2cb_ctl.c o2cb_config.c \
	jconfig.c jiterator.c

O2CB_CTL_OBJS = $(subst .c,.o,$(O2CB_CTL_CFILES))

HEADERS = jconfig.h jiterator.h

CFILES = $(LIBO2CB_CFILES) $(CLUSTERBO_CFILES) $(O2CB_CTL_CFILES)

DIST_FILES = $(CFILES) $(HEADERS)
DIST_RULES = dist-subdircreate

o2cb_abi_CFLAGS = -fPIC
libo2cb.a: $(LIBO2CB_OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

clusterbo: $(CLUSTERBO_OBJS) $(LIBOCFS2_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

jconfig_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED
jiterator_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED
o2cb_config_CPPFLAGS = $(GLIB_CFLAGS) -DG_DISABLE_DEPRECATED
o2cb_ctl: $(O2CB_CTL_OBJS) $(LIBOCFS2_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBO2CB_LIBS) $(GLIB_LIBS) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
