TOPDIR = .
 
include $(TOPDIR)/Preamble.make

RPMBUILD = $(shell /usr/bin/which rpmbuild 2>/dev/null || /usr/bin/which rpm 2>/dev/null || echo /bin/false)

SUSEBUILD = $(shell if test -r /etc/UnitedLinux-release -o -r /etc/SuSE-release; then echo yes; else echo no; fi)

ifeq ($(SUSEBUILD),yes)
GTK_NAME = gtk
CHKCONFIG_DEP = aaa_base
else
GTK_NAME = gtk+
CHKCONFIG_DEP = chkconfig
endif

TOOLSARCH = $(shell $(TOPDIR)/toolsarch.guess $(TOPDIR))

ifeq ($(TOOLSARCH),error)
$(error could not detect architecture for tools)
endif

SUBDIRS = libocfs debugocfs format fsck bugfix load_ocfs ocfs_uid_gen

ifdef OCFSTOOL
SUBDIRS += ocfstool
endif

SUBDIRS += vendor

DIST_FILES = \
	COPYING		\
	README		\
	Config.make.in	\
	Preamble.make	\
	Postamble.make	\
	aclocal.m4	\
	config.guess	\
	config.sub	\
	configure	\
	configure.in	\
	install-sh	\
	mkinstalldirs	\
	toolsarch.guess

.PHONY: dist dist-bye dist-fresh distclean

dist-bye:
	-rm -rf $(DIST_TOPDIR)

dist-fresh: dist-bye
	$(TOPDIR)/mkinstalldirs $(DIST_TOPDIR)

dist: dist-fresh dist-all
	GZIP=$(GZIP_OPTS) tar chozf $(DIST_TOPDIR).tar.gz $(DIST_TOPDIR)
	$(MAKE) dist-bye

distclean: clean
	rm -f Config.make config.status config.cache config.log

srpm: dist
	$(RPMBUILD) -bs --define "_sourcedir $(RPM_TOPDIR)" --define "_srcrpmdir $(RPM_TOPDIR)" --define "gtk_name $(GTK_NAME)" --define "chkconfig_dep $(CHKCONFIG_DEP)" $(TOPDIR)/vendor/common/ocfs-tools.spec

rpm: srpm
	$(RPMBUILD) --rebuild --define "gtk_name $(GTK_NAME)" --define "chkconfig_dep $(CHKCONFIG_DEP)" --target $(TOOLSARCH) "ocfs-tools-$(DIST_VERSION)-$(RPM_VERSION).src.rpm"

def:
	@echo $(TOOLSARCH)

include $(TOPDIR)/Postamble.make
