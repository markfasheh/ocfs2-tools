TOPDIR = ../..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
	-Wmissing-declarations

ifdef OCFS_DEBUG
OPTS += -g
endif

INCLUDES = -I. -Iinclude

LIBRARIES = libocfs2.a

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS)

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
BIN_PROGRAMS += unix_io openfs inode

unix_io: unix_io.c libocfs2.a
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) \
		$(INCLUDES) $(DEFINES) $(VERMAGIC) \
		$(COM_ERR_LIBS) -DDEBUG_EXE -o $@ $^

openfs: openfs.c libocfs2.a
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) \
		$(INCLUDES) $(DEFINES) $(VERMAGIC) \
		$(COM_ERR_LIBS) -DDEBUG_EXE -o $@ $^

inode: inode.c libocfs2.a
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) \
		$(INCLUDES) $(DEFINES) $(VERMAGIC) \
		$(COM_ERR_LIBS) -DDEBUG_EXE -o $@ $^
endif
CFILES = 		\
	unix_io.c	\
	memory.c	\
	openfs.c	\
	closefs.c	\
	freefs.c

HFILES =				\
	include/kernel-jbd.h		\
	include/kernel-list.h		\
	include/ocfs2_fs.h		\
	include/ocfs2_disk_dlm.h	\
	include/ocfs1_fs_compat.h	\
	include/memory.h		\
	include/unix_io.h		\
	include/byteorder.h		\
	include/filesys.h

HFILES_GEN =		\
	ocfs2_err.h
	
$(CFILES): $(HFILES) $(HFILES_GEN)

OBJS = $(subst .c,.o,$(CFILES)) \
	ocfs2_err.o

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
