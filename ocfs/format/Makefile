TOPDIR = ../..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS) -pthread $(GLIB_CFLAGS)

SBIN_PROGRAMS = mkfs.ocfs mounted.ocfs tuneocfs

INCLUDES = -I$(TOPDIR)/ocfs/libocfs/Common/inc -I$(TOPDIR)/ocfs/libocfs/Linux/inc -Iinc -I$(TOPDIR)/ocfs/libocfs
DEFINES = -DLINUX -DUSERSPACE_TOOL -DFORMAT_OCFS

OPTIMIZE = -O2

ifeq ($(OCFS_PROCESSOR),x86_64)
  CFLAGS += -m64
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
  DEFINES += -D__ILP32__
endif

CFLAGS += $(OPTIMIZE)

VERSION_FILES = format.c frmtport.c inc/format.h inc/frmtport.h journal.c mounted.c system.c tune.c inc/tune.h inc/jfs_compat.h inc/kernel-jbd.h inc/kernel-list.h
VERSION_SRC = frmtport.c
VERSION_PREFIX = OCFS

DIST_RULES = dist-incdir

MANS = mkfs.ocfs.8 tuneocfs.8

INSTALL_RULES = install-sbin-links install-man-links

install-sbin-links: install-sbin-programs
	cd $(DESTDIR)$(sbindir) \
	&& rm -f mkfs.ocfs2 resizeocfs \
	&& $(LN_S) mkfs.ocfs mkfs.ocfs2 \
	&& $(LN_S) tuneocfs resizeocfs

install-man-links: install-mans
	cd $(DESTDIR)$(mandir)/man8 \
	&& rm -f mkfs.ocfs.8 resizeocfs.8 \
	&& $(LN_S) mkfs.ocfs.8 mkfs.ocfs2.8 \
	&& $(LN_S) tuneocfs.8 resizeocfs.8

DIST_FILES = $(VERSION_FILES) $(VERSION_SRC) mkfs.ocfs.8.in tuneocfs.8.in

vpath ocfsheartbeat.c $(TOPDIR)/ocfs/libocfs/Common

mkfs.ocfs: format.o frmtport.o journal.o system.o
	$(LINK) -L$(TOPDIR)/ocfs/libocfs -locfs

mounted.ocfs: mounted.o frmtport.o
	$(LINK) -L$(TOPDIR)/ocfs/libocfs -locfs

tuneocfs: tune.o frmtport.o
	$(LINK) -L$(TOPDIR)/ocfs/libocfs -locfs

dist-incdir:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/inc

include $(TOPDIR)/Postamble.make
