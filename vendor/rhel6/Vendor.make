#
# RHEL 6
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/rhel6/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = el6
SYSTEMD_ENABLED = 0
INSTALL_DEP_PKG = "modutils"

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
