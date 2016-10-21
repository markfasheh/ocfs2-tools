#
# Fedora 9
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/fc9/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = fc9
SYSTEMD_ENABLED = 0
INSTALL_DEP_PKG = "modutils"

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
