TOPDIR = ../..

include $(TOPDIR)/Preamble.make

BIN_PROGRAMS = debugfs.ocfs2

DEFINES = -DLINUX -DDEBUGOCFS -DDEBUG -DDEBUGOCFS_VERSION=\"0.0.1\"

INCLUDES = -Iinclude -I../libocfs2/include
INCLUDES += $(GLIB_CFLAGS)

CFLAGS += -Wall -O2

CFILES = main.c commands.c dump.c readfs.c utils.c journal.c bindraw.c
HFILES = include/main.h include/commands.h include/dump.h include/readfs.h include/utils.h include/journal.h include/bindraw.h

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) $(HFILES) README

debugfs.ocfs2: $(OBJS)
	$(LINK) $(GLIB_LIBS) -lreadline -lncurses

include $(TOPDIR)/Postamble.make
