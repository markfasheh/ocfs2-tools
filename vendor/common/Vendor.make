#
# Support files
#

# This Vendor.make expects TOOLSARCH and VENDOR_EXTENSION to be set by an
# including Makefile.


RPMBUILD = $(shell /usr/bin/which rpmbuild 2>/dev/null || /usr/bin/which rpm 2>/dev/null || echo /bin/false)
RPM_TOPDIR = `pwd`

ifndef VENDOR_EXTENSION
VENDOR_EXTENSION = common
endif

$(TOPDIR)/ocfs2-tools-$(DIST_VERSION)-$(PKG_VERSION).$(VENDOR_EXTENSION).src.rpm: dist $(TOPDIR)/vendor/common/ocfs2-tools.spec-generic
	sed -e 's,@@PKG_VERSION@@,'$(PKG_VERSION)',g' \
		-e 's,@@VENDOR_EXTENSION@@,'$(VENDOR_EXTENSION)',g' \
		-e 's,@@PYGTK_NAME@@,'$(PYGTK_NAME)',g' \
		-e 's,@@PYVERSION@@,'$(PYVERSION)',g' \
		-e 's,@@COMPILE_PY@@,'$(COMPILE_PY)',g' \
		-e 's,@@CHKCONFIG_DEP@@,'$(CHKCONFIG_DEP)',g' \
		-e 's,@@SYSTEMD_ENABLED@@,'$(SYSTEMD_ENABLED)',g' \
		< "$(TOPDIR)/vendor/common/ocfs2-tools.spec-generic" \
		> "$(TOPDIR)/vendor/common/ocfs2-tools.spec"
	$(RPMBUILD) -bs --define "_sourcedir $(RPM_TOPDIR)" --define "_srcrpmdir $(RPM_TOPDIR)" "$(TOPDIR)/vendor/common/ocfs2-tools.spec"
	rm "$(TOPDIR)/vendor/common/ocfs2-tools.spec"

srpm: $(TOPDIR)/ocfs2-tools-$(DIST_VERSION)-$(PKG_VERSION).$(VENDOR_EXTENSION).src.rpm

rpm: srpm
	$(RPMBUILD) --rebuild $(TOOLSARCH) "ocfs2-tools-$(DIST_VERSION)-$(PKG_VERSION).$(VENDOR_EXTENSION).src.rpm"

