TOPDIR = ..

include $(TOPDIR)/Preamble.make

sbindir = $(root_sbindir)
BIN_PROGRAMS = puncher

DEFINES = -DG_DISABLE_DEPRECATED -DLINUX -DDEBUG
DEFINES += -DVERSION=\"$(VERSION)\"

ifeq ($(HAVE_FALLOCATE),1)
DEFINES += -DHAVE_FALLOCATE
endif

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

CFLAGS += $(WARNINGS)

LIBTOOLS_INTERNAL_LIBS = -L$(TOPDIR)/libtools-internal -ltools-internal
LIBTOOLS_INTERNAL_DEPS = $(TOPDIR)/libtools-internal/libtools-internal.a

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

INCLUDES = -I$(TOPDIR)/include -Iinclude
INCLUDES += $(GLIB_CFLAGS)

CFILES = puncher.c

HFILES =

OBJS = $(subst .c,.o,$(CFILES))

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2

MANS =

DIST_FILES = $(CFILES) $(HFILES)

DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

puncher: $(OBJS) ${LIBOCFS2_DEPS} ${LIBTOOLS_INTERNAL_DEPS}
	$(LINK) $(GLIB_LIBS) $(LIBOCFS2_LIBS) ${LIBTOOLS_INTERNAL_LIBS} $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
