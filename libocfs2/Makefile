TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wmissing-declarations

ifdef OCFS_DEBUG
OPTS += -g
endif

INCLUDES = -Iinclude

LIBRARIES = libocfs2.a

CFLAGS = $(OPTS) $(WARNINGS) -fPIC
CPPFLAGS += -DOCFS2_FLAT_INCLUDES

OPTIMIZE = -O2

CFLAGS += $(OPTIMIZE)

ifneq ($(OCFS2_DEBUG_EXE),)
DEBUG_EXE_FILES = $(shell awk '/DEBUG_EXE/{if (k[FILENAME] == 0) {print FILENAME; k[FILENAME] = 1;}}' $(CFILES))
DEBUG_EXE_PROGRAMS = $(addprefix debug_,$(subst .c,,$(DEBUG_EXE_FILES)))

.SECONDARY:

UNINST_PROGRAMS += $(DEBUG_EXE_PROGRAMS)

debug_%.o : %.c 
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) \
		$(INCLUDES) $(DEFINES) \
		-DDEBUG_EXE -o $@ -c $<

debug_%: debug_%.o libocfs2.a
	$(LINK) $(COM_ERR_LIBS)

endif

CFILES = 		\
	unix_io.c	\
	memory.c	\
	openfs.c	\
	closefs.c	\
	freefs.c	\
	getsize.c	\
	inode.c		\
	inode_scan.c	\
	ismounted.c	\
	cached_inode.c	\
	extent_map.c	\
	mkjournal.c	\
	extents.c	\
	dirblock.c	\
	dir_iterate.c	\
	lookup.c	\
	sysfile.c	\
	link.c		\
	unlink.c	\
	bitmap.c	\
	fileio.c	\
	chain.c		\
	chainalloc.c	\
	checkhb.c	\
	kernel-rbtree.c	\
	bitops.c

HFILES =				\
	include/jfs_user.h		\
	include/jfs_compat.h		\
	include/kernel-jbd.h		\
	include/kernel-list.h		\
	include/kernel-rbtree.h		\
	include/ocfs2_fs.h		\
	include/ocfs2_disk_dlm.h	\
	include/ocfs1_fs_compat.h	\
	include/byteorder.h		\
	include/ocfs2.h			\
	include/dir_iterate.h		\
	include/extent_map.h		\
	include/bitmap.h		\
	include/bitops.h

HFILES_GEN =		\
	include/ocfs2_err.h
	
$(CFILES): $(HFILES) $(HFILES_GEN)

OBJS = $(subst .c,.o,$(CFILES)) \
	ocfs2_err.o

ocfs2_err.et: ocfs2_err.et.in
	cd $(TOPDIR) && ./config.status

include/ocfs2_err.h: ocfs2_err.h
	cp $< $@

ocfs2_err.c ocfs2_err.h: ocfs2_err.et
	compile_et ocfs2_err.et

libocfs2.a: $(OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

DIST_FILES = $(CFILES) $(HFILES) ocfs2_err.et.in

DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

include $(TOPDIR)/Postamble.make
