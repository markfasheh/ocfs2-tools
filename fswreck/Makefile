TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = fswreck

DEFINES = -DG_DISABLE_DEPRECATED -DLINUX
DEFINES += -DOCFS2_FLAT_INCLUDES -DVERSION=\"$(VERSION)\"

INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include
INCLUDES += $(GLIB_CFLAGS)

ifdef OCFS_DEBUG
CFLAGS = -Wall -O -ggdb
else
CFLAGS = -Wall -O2
endif

CFILES = main.c

HFILES = 			\
	include/main.h

OBJS = $(subst .c,.o,$(CFILES))

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2

fswreck: $(OBJS)
	$(LINK) $(GLIB_LIBS) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
