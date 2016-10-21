#
# SLES 10
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/sles10/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = SLE10
SYSTEMD_ENABLED = 0
INSTALL_DEP_PKG = "redhat-lsb\,\ modutils"

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
