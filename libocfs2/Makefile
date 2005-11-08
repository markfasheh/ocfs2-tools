TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wmissing-declarations

ifdef OCFS2_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

INCLUDES = -Iinclude -I$(TOPDIR)/libo2dlm/include -I$(TOPDIR)/libo2cb/include

LIBRARIES = libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

CFLAGS = $(OPTS) $(WARNINGS) -fPIC
CPPFLAGS += -DOCFS2_FLAT_INCLUDES -DO2DLM_FLAT_INCLUDES -DO2CB_FLAT_INCLUDES

ifneq ($(OCFS2_DEBUG_EXE),)
DEBUG_EXE_FILES = $(shell awk '/DEBUG_EXE/{if (k[FILENAME] == 0) {print FILENAME; k[FILENAME] = 1;}}' $(CFILES))
DEBUG_EXE_PROGRAMS = $(addprefix debug_,$(subst .c,,$(DEBUG_EXE_FILES)))

.SECONDARY:

UNINST_PROGRAMS += $(DEBUG_EXE_PROGRAMS)

debug_%.o : %.c 
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) \
		$(INCLUDES) $(DEFINES) \
		-DDEBUG_EXE -o $@ -c $<

debug_%: debug_%.o libocfs2.a $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(COM_ERR_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS)

endif

CFILES = 		\
	alloc.c		\
	bitmap.c	\
	bitops.c	\
	cached_inode.c	\
	chain.c		\
	chainalloc.c	\
	checkhb.c	\
	closefs.c	\
	dirblock.c	\
	dir_iterate.c	\
	dir_scan.c	\
	dlm.c		\
	fileio.c	\
	freefs.c	\
	expanddir.c	\
	extend_file.c	\
	extents.c	\
	extent_map.c	\
	getsectsize.c	\
	getsize.c	\
	heartbeat.c	\
	inode.c		\
	inode_scan.c	\
	ismounted.c	\
	kernel-rbtree.c	\
	link.c		\
	lookup.c	\
	memory.c	\
	mkjournal.c	\
	namei.c		\
	newdir.c	\
	openfs.c	\
	sysfile.c	\
	truncate.c	\
	unix_io.c	\
	unlink.c	\
	lockid.c

HFILES =				\
	include/bitmap.h		\
	include/bitops.h		\
	include/byteorder.h		\
	include/dir_iterate.h		\
	include/dir_util.h		\
	include/extent_map.h		\
	include/jfs_compat.h		\
	include/jfs_user.h		\
	include/kernel-jbd.h		\
	include/kernel-list.h		\
	include/kernel-rbtree.h		\
	include/ocfs1_fs_compat.h	\
	include/ocfs2_lockid.h		\
	include/ocfs2_fs.h		\
	include/ocfs2.h

HFILES_GEN =		\
	include/ocfs2_err.h

OBJS = $(subst .c,.o,$(CFILES)) \
	ocfs2_err.o

$(OBJS): $(HFILES_GEN)

include/ocfs2_err.h: ocfs2_err.h
	cp $< $@

ocfs2_err.c ocfs2_err.h: ocfs2_err.et
	compile_et ocfs2_err.et

libocfs2.a: $(OBJS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

DIST_FILES = $(CFILES) $(HFILES) ocfs2_err.et

DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

CLEAN_RULES = clean-err

clean-err:
	rm -f ocfs2_err.c ocfs2_err.h include/ocfs2_err.h

include $(TOPDIR)/Postamble.make
