TOPDIR = ..

include $(TOPDIR)/Preamble.make

INCLUDES = -I$(TOPDIR)/include

LIBRARIES = libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

CFLAGS += -fPIC

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
	slot_map.c	\
	sysfile.c	\
	truncate.c	\
	unix_io.c	\
	unlink.c	\
	lockid.c	\
	backup_super.c	\
	feature_string.c

HFILES =		\
	bitmap.h	\
	dir_iterate.h	\
	dir_util.h	\
	dlm.h		\
	extent_map.h

HFILES_GEN = ocfs2_err.h

OBJS = $(subst .c,.o,$(CFILES)) \
	ocfs2_err.o

$(OBJS): $(HFILES_GEN)

ocfs2_err.c ocfs2_err.h: ocfs2_err.et
	compile_et ocfs2_err.et

libocfs2.a: $(OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

DIST_FILES = $(CFILES) $(HFILES) ocfs2_err.et

CLEAN_RULES = clean-err

clean-err:
	rm -f ocfs2_err.c ocfs2_err.h

include $(TOPDIR)/Postamble.make
