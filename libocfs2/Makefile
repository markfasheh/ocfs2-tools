TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wmissing-declarations

ifdef OCFS_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

INCLUDES = -Iinclude -I$(TOPDIR)/libo2dlm/include

LIBRARIES = libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

CFLAGS = $(OPTS) $(WARNINGS) -fPIC
CPPFLAGS += -DOCFS2_FLAT_INCLUDES -DO2DLM_FLAT_INCLUDES

ifneq ($(OCFS2_DEBUG_EXE),)
DEBUG_EXE_FILES = $(shell awk '/DEBUG_EXE/{if (k[FILENAME] == 0) {print FILENAME; k[FILENAME] = 1;}}' $(CFILES))
DEBUG_EXE_PROGRAMS = $(addprefix debug_,$(subst .c,,$(DEBUG_EXE_FILES)))

.SECONDARY:

UNINST_PROGRAMS += $(DEBUG_EXE_PROGRAMS)

debug_%.o : %.c 
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) \
		$(INCLUDES) $(DEFINES) \
		-DDEBUG_EXE -o $@ -c $<

debug_%: debug_%.o libocfs2.a $(LIBO2DLM_DEPS)
	$(LINK) $(COM_ERR_LIBS) $(LIBO2DLM_LIBS)

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
	alloc.c		\
	checkhb.c	\
	kernel-rbtree.c	\
	bitops.c	\
	expanddir.c	\
	newdir.c	\
	extend_file.c	\
	namei.c		\
	truncate.c	\
	dlm.c

HFILES =				\
	include/jfs_user.h		\
	include/jfs_compat.h		\
	include/kernel-jbd.h		\
	include/kernel-list.h		\
	include/kernel-rbtree.h		\
	include/ocfs2_fs.h		\
	include/ocfs1_fs_compat.h	\
	include/byteorder.h		\
	include/ocfs2.h			\
	include/dir_iterate.h		\
	include/extent_map.h		\
	include/bitmap.h		\
	include/bitops.h

HFILES_GEN =		\
	include/ocfs2_err.h
	
OBJS = $(subst .c,.o,$(CFILES)) \
	ocfs2_err.o

$(OBJS): $(HFILES_GEN)

ocfs2_err.et: ocfs2_err.et.in
	cd $(TOPDIR) && ./config.status

include/ocfs2_err.h: ocfs2_err.h
	cp $< $@

ocfs2_err.c ocfs2_err.h: ocfs2_err.et
	compile_et ocfs2_err.et

libocfs2.a: $(OBJS) $(LIBO2DLM_DEPS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

DIST_FILES = $(CFILES) $(HFILES) ocfs2_err.et.in

DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

CLEAN_RULES = clean-err

clean-err:
	rm -f ocfs2_err.c ocfs2_err.h include/ocfs2_err.h

include $(TOPDIR)/Postamble.make
