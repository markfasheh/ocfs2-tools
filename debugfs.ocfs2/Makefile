TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = debugfs.ocfs2

DEFINES = -DG_DISABLE_DEPRECATED -DLINUX -DDEBUGOCFS -DDEBUG
DEFINES += -DOCFS2_FLAT_INCLUDES -DVERSION=\"$(VERSION)\"

INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include
INCLUDES += $(GLIB_CFLAGS)

CFLAGS += -Wall -O2

CFILES = main.c commands.c dump.c readfs.c utils.c journal.c
HFILES =			\
	include/main.h		\
	include/commands.h	\
	include/dump.h		\
	include/readfs.h	\
	include/utils.h		\
	include/jbd.h		\
	include/journal.h

OBJS = $(subst .c,.o,$(CFILES))

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2

MANS = debugfs.ocfs2.8

DIST_FILES = $(CFILES) $(HFILES) README debugfs.ocfs2.8.in

DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

debugfs.ocfs2: $(OBJS)
	$(LINK) $(GLIB_LIBS) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS) -lreadline -lncurses

include $(TOPDIR)/Postamble.make
