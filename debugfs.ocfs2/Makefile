TOPDIR = ..

include $(TOPDIR)/Preamble.make

sbindir = $(root_sbindir)
SBIN_PROGRAMS = debugfs.ocfs2

DEFINES = -DG_DISABLE_DEPRECATED -DLINUX -DDEBUG
DEFINES += -DVERSION=\"$(VERSION)\"

INCLUDES = -I$(TOPDIR)/include -Iinclude
INCLUDES += $(GLIB_CFLAGS)

CFILES = main.c commands.c dump.c utils.c journal.c find_block_inode.c \
	find_inode_paths.c dump_fs_locks.c dump_dlm_locks.c stat_sysdir.c \
	dump_net_stats.c

HFILES =				\
	include/main.h			\
	include/commands.h		\
	include/dump.h			\
	include/utils.h			\
	include/journal.h		\
	include/find_block_inode.h	\
	include/find_inode_paths.h	\
	include/ocfs2_internals.h	\
	include/dump_fs_locks.h		\
	include/dump_dlm_locks.h	\
	include/stat_sysdir.h		\
	include/dump_net_stats.h

OBJS = $(subst .c,.o,$(CFILES))

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb

MANS = debugfs.ocfs2.8

DIST_FILES = $(CFILES) $(HFILES) README debugfs.ocfs2.8.in

DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

debugfs.ocfs2: $(OBJS)
	$(LINK) $(GLIB_LIBS) $(LIBOCFS2_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS) $(READLINE_LIBS) $(NCURSES_LIBS)

include $(TOPDIR)/Postamble.make
