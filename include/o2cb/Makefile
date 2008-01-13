TOPDIR = ../..

include $(TOPDIR)/Preamble.make

HFILES_GEN = o2cb_err.h

all: $(HFILES_GEN)

HFILES = o2cb.h ocfs2_nodemanager.h ocfs2_heartbeat.h o2cb_client_proto.h

HEADERS_SUBDIR = o2cb
HEADERS = $(HFILES) $(HFILES_GEN)

o2cb_err.h: $(TOPDIR)/libo2cb/o2cb_err.h
	cp $< $@

$(TOPDIR)/libo2cb/o2cb_err.h:
	make -C $(TOPDIR)/libo2cb o2cb_err.h

DIST_FILES = $(HFILES)

CLEAN_RULES = clean-err

clean-err:
	rm -f o2cb_err.h

include $(TOPDIR)/Postamble.make
