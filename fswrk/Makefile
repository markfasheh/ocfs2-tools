TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = fswrk

DEFINES = -DG_DISABLE_DEPRECATED -DLINUX
DEFINES += -DVERSION=\"$(VERSION)\"

INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include
INCLUDES += $(GLIB_CFLAGS)

ifdef OCFS_DEBUG
CFLAGS = -Wall -O -ggdb
else
CFLAGS = -Wall -O2
endif

CFILES = main.c readfs.c utils.c

HFILES = 			\
	include/main.h		\
	include/readfs.h	\
	include/utils.h

OBJS = $(subst .c,.o,$(CFILES))

fswrk: $(OBJS)
	$(LINK) $(GLIB_LIBS) $(LIBOCFS2_LIBS)

include $(TOPDIR)/Postamble.make
