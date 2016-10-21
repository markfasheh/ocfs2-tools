#
# Fedora 8
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/fc8/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = fc8
SYSTEMD_ENABLED = 0
INSTALL_DEP_PKG = "redhat-lsb\,\ modutils"

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
