TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -O -ggdb
else
OPTS = -O2
endif

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS) 

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

SBIN_PROGRAMS = tunefs.ocfs2

INCLUDES = -I$(TOPDIR)/libocfs2/include $(GLIB_CFLAGS)
DEFINES = -DOCFS2_FLAT_INCLUDES -DVERSION=\"$(VERSION)\"

#MANS = tunefs.ocfs2.8

CFILES = tunefs.c
OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES)

tunefs.ocfs2: $(OBJS) $(LIBOCFS2_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(GLIB_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
