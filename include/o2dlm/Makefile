TOPDIR = ../..

include $(TOPDIR)/Preamble.make

HFILES_GEN = o2dlm_err.h

all: $(HFILES_GEN)

HFILES = o2dlm.h

HEADERS_SUBDIR = o2dlm
HEADERS = $(HFILES) $(HFILES_GEN)

o2dlm_err.h: $(TOPDIR)/libo2dlm/o2dlm_err.h
	cp $< $@

$(TOPDIR)/libo2dlm/o2dlm_err.h:
	make -C $(TOPDIR)/libo2dlm o2dlm_err.h

DIST_FILES = $(HFILES)

CLEAN_RULES = clean-err

clean-err:
	rm -f o2dlm_err.h

include $(TOPDIR)/Postamble.make
