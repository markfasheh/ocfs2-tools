#
# Fedora 7
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/fc7/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = fc7
SYSTEMD_ENABLED = 0
INSTALL_DEP_PKG = "redhat-lsb\,\ modutils"

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
