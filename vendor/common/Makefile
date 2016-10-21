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

MANS = o2cb.sysconfig.5

DIST_FILES =					\
	$(SCRIPT_SH)				\
	o2cb.sysconfig				\
	ocfs2.init				\
	ocfs2-tools.spec-generic.in		\
	51-ocfs2.rules				\
	Vendor.make				\
	o2cb.sysconfig.5.in		\
	o2cb.service			\
	ocfs2.service

include $(TOPDIR)/Postamble.make
