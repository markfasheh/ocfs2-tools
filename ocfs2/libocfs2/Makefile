TOPDIR = ../..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
	-Wmissing-declarations

ifdef OCFS_DEBUG
OPTS += -g
endif

INCLUDES = -Iinclude

LIBRARIES = libocfs2.a

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS)
CPPFLAGS += -DOCFS2_FLAT_INCLUDES

OPTIMIZE = -O2

ifeq ($(OCFS_PROCESSOR),ppc64)
endif
ifeq ($(OCFS_PROCESSOR),x86_64)
  CFLAGS += -m64
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
  DEFINES += -D__ILP32__
endif

CFLAGS += $(OPTIMIZE)

ifneq ($(OCFS2_DEBUG_EXE),)
DEBUG_EXE_FILES = $(shell awk '/DEBUG_EXE/{if (k[FILENAME] == 0) {print FILENAME; k[FILENAME] = 1;}}' $(CFILES))
DEBUG_EXE_PROGRAMS = $(addprefix debug_,$(subst .c,,$(DEBUG_EXE_FILES)))

UNINST_PROGRAMS += $(DEBUG_EXE_PROGRAMS)

debug_%: %.c libocfs2.a
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) \
		$(INCLUDES) $(DEFINES) $(VERMAGIC) \
		$(COM_ERR_LIBS) -DDEBUG_EXE -o $@ $^

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
	bitmap.c

HFILES =				\
	include/jfs_user.h		\
	include/jfs_compat.h		\
	include/kernel-jbd.h		\
	include/kernel-list.h		\
	include/ocfs2_fs.h		\
	include/ocfs2_disk_dlm.h	\
	include/ocfs1_fs_compat.h	\
	include/byteorder.h		\
	include/ocfs2.h			\
	include/dir_iterate.h		\
	include/extent_map.h		\
	include/bitmap.h

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
