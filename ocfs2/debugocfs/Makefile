TOPDIR = ../..

include $(TOPDIR)/Preamble.make

BIN_PROGRAMS = debugfs.ocfs

DEFINES = -DLINUX -DDEBUGOCFS -DDEBUG -DDEBUGOCFS_VERSION=\"0.0.1\"

INCLUDES = -Iinc -I../libocfs2/include
INCLUDES += $(GLIB_CFLAGS)

CFLAGS += -Wall -O2

CFILES = main.c commands.c dump.c readfs.c
HFILES = main.h commands.h dump.h readfs.h

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) $(HFILES) README

debugfs.ocfs: $(OBJS)
	$(LINK) $(GLIB_LIBS) -lreadline -lncurses

include $(TOPDIR)/Postamble.make
