TOPDIR = ..

include $(TOPDIR)/Preamble.make

ifdef HAVE_BLKID
BLKID_DIR =
else
BLKID_DIR = blkid
endif

SUBDIRS = $(BLKID_DIR) ocfs2interface

MANS = ocfs2console.8

SBIN_EXTRA = ocfs2console

DIST_FILES = ocfs2console ocfs2console.8.in

include $(TOPDIR)/Postamble.make
