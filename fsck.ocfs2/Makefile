TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = fsck.ocfs2

INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

ifdef OCFS_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

CFLAGS := $(OPTS) -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

DEFINES = -DOCFS2_FLAT_INCLUDES

CFILES =	fsck.c		\
		dirblocks.c 	\
		dirparents.c 	\
		extent.c 	\
		icount.c 	\
		journal.c 	\
		pass0.c 	\
		pass1.c 	\
		pass2.c 	\
		pass3.c 	\
		pass4.c 	\
		problem.c 	\
		strings.c 	\
		util.c

HFILES = 	include/fsck.h		\
		include/dirblocks.h	\
		include/dirparents.h	\
		include/extent.h	\
		include/icount.h	\
		include/journal.h	\
		include/pass0.h		\
		include/pass1.h		\
		include/pass2.h		\
		include/pass3.h		\
		include/pass4.h		\
		include/problem.h	\
		include/strings.h	\
		include/util.h

OBJS = $(subst .c,.o,$(CFILES))
MANS = fsck.ocfs2.8 fsck.ocfs2.checks.8

DIST_FILES = $(CFILES) $(HFILES) $(addsuffix .in,$(MANS))
DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

fsck.ocfs2: $(OBJS) $(LIBOCFS2_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

.PHONY: check-prompt-dups
check-prompt-dups: fsck.ocfs2
	@rm -f .$@.tmp
	@nm $^ | \
		awk -F'[ .]' '/fsck_prompt_callers_with_code/ {print $$3}' | \
		sort | uniq -c | awk '($$1 > 1){print $$0}' > .$@.tmp
	@cat .$@.tmp
	@test ! -s .$@.tmp

include $(TOPDIR)/Postamble.make
