#
# RHEL 5
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/rhel5/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = el5
SYSTEMD_ENABLED = 0
INSTALL_DEP_PKG = "modutils"

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
