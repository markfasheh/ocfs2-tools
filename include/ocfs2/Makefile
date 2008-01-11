TOPDIR = ../..

include $(TOPDIR)/Preamble.make

HFILES_GEN = ocfs2_err.h

all: $(HFILES_GEN)

HFILES = ocfs2.h jbd.h bitops.h byteorder.h kernel-rbtree.h feature_string.h

HEADERS_SUBDIR = ocfs2
HEADERS = $(HFILES) $(HFILES_GEN)

ocfs2_err.h: $(TOPDIR)/libocfs2/ocfs2_err.h
	cp $< $@

$(TOPDIR)/libocfs2/ocfs2_err.h:
	make -C $(TOPDIR)/libocfs2 ocfs2_err.h

DIST_FILES = $(HFILES)

CLEAN_RULES = clean-err

clean-err:
	rm -f ocfs2_err.h

include $(TOPDIR)/Postamble.make
