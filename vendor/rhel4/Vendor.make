#
# RHEL 4
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/rhel4/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = el4
SYSTEMD_ENABLED = 0
INSTALL_DEP_PKG = "redhat-lsb\,\ modutils"

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
