TOPDIR = ../..

include $(TOPDIR)/Preamble.make

BIN_PROGRAMS = debugfs.ocfs

DEFINES = -DLINUX -DDEBUGOCFS -DDEBUG -DDEBUGOCFS_VERSION=\"0.0.1\"

#INCLUDES = -I. -I$(TOPDIR)/ocfs2/Common/inc -I$(TOPDIR)/ocfs2/Linux/inc
INCLUDES = -Iinc -I/home/smushran/lkocfs/src
INCLUDES += $(GLIB_CFLAGS)

CFLAGS += -Wall -O2

#vpath ocfsgen%.c $(TOPDIR)/ocfs2/Common
#vpath ocfs%.c $(TOPDIR)/ocfs2/Linux

CFILES = main.c commands.c dump.c readfs.c
HFILES = main.h commands.h dump.h readfs.h

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) $(HFILES) README

#GEN_SRC_HEADERS = $(TOPDIR)/ocfs2/Common/inc/ocfsdisk.h $(TOPDIR)/ocfs2/Common/inc/ocfsvol.h $(TOPDIR)/ocfs2/Common/inc/ocfsconst.h $(TOPDIR)/ocfs2/Common/inc/ocfstrans.h

#stamp-ops: gencode.pl $(GEN_SRC_HEADERS)
#	./gencode.pl $(GEN_SRC_HEADERS)
#	touch stamp-ops

debugfs.ocfs: $(OBJS)
	$(LINK) $(GLIB_LIBS) -lreadline -lncurses

include $(TOPDIR)/Postamble.make
