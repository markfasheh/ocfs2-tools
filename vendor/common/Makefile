TOPDIR = ../..

include $(TOPDIR)/Preamble.make

SCRIPT_SH = o2cb.init.sh
SCRIPTS = $(patsubst %.sh,%,$(SCRIPT_SH))

# Depend on the substitution values
$(SCRIPTS): $(TOPDIR)/Config.make

$(patsubst %.sh,%,$(SCRIPT_SH)) : % : %.sh
	rm -f $@ $@+
	sed -e 's/@@VERSION@@/$(VERSION)/g' $@.sh >$@+
	chmod +x $@+
	mv $@+ $@

ALL_RULES = $(SCRIPTS)

DIST_FILES =					\
	$(SCRIPT_SH)				\
	o2cb.sysconfig				\
	ocfs2.init				\
	ocfs2-tools.spec-generic.in		\
	Vendor.make

include $(TOPDIR)/Postamble.make
