TOPDIR = ..

include $(TOPDIR)/Preamble.make

ifdef OCFS_DEBUG
  OPTS = -g
endif

SBIN_PROGRAMS = debugocfs ocfsextfinder

LIBRARIES = libdebugocfs.a

# Too many warnings in 1.0 branch (cleaned in 2.0)
#WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
#           -Wmissing-declarations

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS)

INCLUDES = -I. -I$(TOPDIR)/libocfs/Common/inc -I$(TOPDIR)/libocfs/Linux/inc -I$(TOPDIR)/libocfs
DEFINES = -DLINUX -DUSERSPACE_TOOL -DDEBUGOCFS

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

ifdef OCFS_DEBUG
  DEFINES += -DDEBUG
endif

extfinder_CFLAGS = $(GLIB_CFLAGS)

test_CFLAGS = $(GLIB_CFLAGS)

CLEAN_FILES = test

MANS = debugocfs.8

VERSION_FILES = debugocfs.c debugocfs.h libdebugocfs.h test.c print.c libdebugocfs.c io.c main.c extfinder.c
VERSION_SRC = main.c extfinder.c
VERSION_PREFIX = OCFS

DIST_FILES = $(VERSION_FILES) $(VERSION_SRC) debugocfs.8.in debugocfs.txt

test: test.o libdebugocfs.a
	$(LINK) -L. -ldebugocfs $(GTK_LIBS)

ocfsextfinder: extfinder.o ../libocfs/libocfs.a
	$(LINK) $(GLIB_LIBS) -lglib 

debugocfs: print.o debugocfs.o io.o main.o ../libocfs/libocfs.a
	$(LINK) -pthread -L$(TOPDIR)/libocfs -locfs

dbg.o: debugocfs.c
	$(CC) $(CFLAGS) $(INCLUDES) $(GTK_CFLAGS) -DLIBDEBUGOCFS $(DEFINES) -o $@ -c $<

libdebugocfs.o: libdebugocfs.c
	$(CC) $(CFLAGS) $(INCLUDES) $(GTK_CFLAGS) -DLIBDEBUGOCFS $(DEFINES) -o $@ -c $<

libdebugocfs.a: io.o dbg.o libdebugocfs.o
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

include $(TOPDIR)/Postamble.make
