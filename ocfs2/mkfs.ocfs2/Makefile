TOPDIR = ../..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS) 

SBIN_PROGRAMS = mkfs.ocfs2 mkfs.ocfs2.kurt

LIBOCFS2_CFLAGS = -I$(TOPDIR)/ocfs2/libocfs2/include
LIBOCFS2_LIBS = -L$(TOPDIR)/ocfs2/libocfs2 -locfs2

INCLUDES = $(LIBOCFS2_CFLAGS)
DEFINES = -DLINUX -DUSERSPACE_TOOL -DFORMAT_OCFS -DOCFS2_FLAT_INCLUDES

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

VERSION_FILES = mkfs.c
VERSION_SRC = mkfs.c
VERSION_PREFIX = OCFS2

DIST_RULES = dist-incdir

#MANS = mkfs.ocfs2.8

INSTALL_RULES = install-sbin-links install-man-links

##########################
# WARNING!!!
# are we still going to do argv[0] tricks?
##########################

#install-sbin-links: install-sbin-programs
#	cd $(DESTDIR)$(sbindir) \
#	&& rm -f mkfs.ocfs2 resizeocfs \
#	&& $(LN_S) mkfs.ocfs mkfs.ocfs2 \
#	&& $(LN_S) tuneocfs resizeocfs

#install-man-links: install-mans
#	cd $(DESTDIR)$(mandir)/man8 \
#	&& rm -f mkfs.ocfs.8 resizeocfs.8 \
#	&& $(LN_S) mkfs.ocfs.8 mkfs.ocfs2.8 \
#	&& $(LN_S) tuneocfs.8 resizeocfs.8

DIST_FILES = $(VERSION_FILES) $(VERSION_SRC) mkfs2.c #mkfs.ocfs2.8.in

mkfs.ocfs2: mkfs.o
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

mkfs.ocfs2.kurt: mkfs2.o
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

readdir: readdir.o
	$(LINK) 

fe: readdir.o
	$(LINK) 

dist-incdir:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/inc

include $(TOPDIR)/Postamble.make
