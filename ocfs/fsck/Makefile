TOPDIR = ../..

include $(TOPDIR)/Preamble.make

MANS = fsck.ocfs.8

SBIN_PROGRAMS = fsck.ocfs blked.ocfs
DIST_FILES = mkgetconst fsck.h classes.h  defaults.h fsck_print.h layout.h sig.h verify.h mkgetconst classes.c parse fsck.ocfs.8.in

ifdef OCFS_DEBUG
  OPTS = -g
endif

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

LIBOCFS_LIBS = -L$(TOPDIR)/ocfs/libocfs -locfs
LIBOCFS_CFLAGS = -pthread -I$(TOPDIR)/ocfs/libocfs

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS) $(LIBOCFS_CFLAGS)
INCLUDES = -I. -I$(TOPDIR)/ocfs/libocfs/Common/inc -I$(TOPDIR)/ocfs/libocfs/Linux/inc

DEFINES = -DLINUX -DUSERSPACE_TOOL -DSHOW_INVALID_VALUES -DFSCK_OCFS

OPTIMIZE = -O2

ifeq ($(OCFS_PROCESSOR),x86_64)
  CFLAGS += -m64
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
  DEFINES += -D__ILP32__
endif
ifeq ($(OCFS_PROCESSOR),s390x)
  DEFINES += -D__LP64__
endif

CFLAGS += $(OPTIMIZE)

ifdef OCFS_DEBUG
  DEFINES += -DDEBUG
endif

GLIB_OBJS = verify.o fsck_io.o class_print.o class_rw.o fsck_print.o sig.o layout.o defaults.o utils.o

## FIXME: make real later
VERSION_FILES = fsck.c verify.c fsck_io.c class_print.c class_rw.c fsck_print.c sig.c layout.c defaults.c ver.c parse.c utils.c blked.c
VERSION_SRC = ver.c
VERSION_PREFIX = FSCK

parse: parse.c
	gcc $(INCLUDES) -D_GNU_SOURCE parse.c -o $@

classes.c: parse
	export COMMON_HEADERS=$(TOPDIR)/ocfs/libocfs/Common/inc; cat $(TOPDIR)/ocfs/libocfs/Common/inc/ocfsdisk.h $(TOPDIR)/ocfs/libocfs/Common/inc/ocfsvol.h | ./parse > $@

classes.o: classes.c
	$(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) $(GLIB_CFLAGS) -o $@ -c $<

fsck.o: fsck.c
	$(CC) $(CFLAGS) $(INCLUDES) $(GLIB_CFLAGS) $(DEFINES) -DUSE_MALLOC_ALIGNED $(VERMAGIC) -o $@ -c $<

blked.o: blked.c
	$(CC) $(CFLAGS) $(INCLUDES) $(GLIB_CFLAGS) $(DEFINES) -DUSE_MALLOC_ALIGNED $(VERMAGIC) -o $@ -c $<

$(GLIB_OBJS): %.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(GLIB_CFLAGS) $(DEFINES) -DUSE_MALLOC_ALIGNED $(VERMAGIC) -o $@ -c $<

fsck.ocfs: classes.o fsck.o $(GLIB_OBJS) ver.o
	$(LINK) $(GLIB_LIBS) $(LIBOCFS_LIBS)

blked.ocfs: classes.o blked.o $(GLIB_OBJS) ver.o
	$(LINK) $(GLIB_LIBS) $(LIBOCFS_LIBS)

include $(TOPDIR)/Postamble.make
