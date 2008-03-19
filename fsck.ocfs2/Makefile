TOPDIR = ..

include $(TOPDIR)/Preamble.make

sbindir = $(root_sbindir)
SBIN_PROGRAMS = fsck.ocfs2

INCLUDES = -I$(TOPDIR)/include -Iinclude
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a
LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm -ldl
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a
LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

ifndef OCFS2_DYNAMIC_FSCK
LDFLAGS += -static
endif

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

fsck.ocfs2: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS)

$(OBJS): prompt-codes.h

# make it hard to add a prompt call
# without documenting what it does
prompt-codes.h: fsck.ocfs2.checks.8.in
	@echo generating $@ by parsing $^
	@rm -f .$@.tmp
	@(	echo '#ifndef __PROMPT_CODES_H__';			\
		echo '#define __PROMPT_CODES_H__';			\
		for a in `grep '^.SS ".*"' $^ | awk -F\" '{print $$2}'`; do\
			echo '#define PR_'$$a' \';			\
			echo '	(struct prompt_code){ .str = "'$$a'" }';\
		done;							\
		echo '#endif';						\
	 ) > $@

.PHONY: check-prompt-dups
check-prompt-dups: fsck.ocfs2
	@rm -f .$@.tmp
	@nm $^ | \
		awk -F'[ .]' '/fsck_prompt_callers_with_code/ {print $$3}' | \
		sort | uniq -c | awk '($$1 > 1){print $$0}' > .$@.tmp
	@cat .$@.tmp
	@test ! -s .$@.tmp

CLEAN_RULES += o2fsck-clean
.PHONY: o2fsck-clean
o2fsck-clean:
	rm -f prompt-codes.h

include $(TOPDIR)/Postamble.make
