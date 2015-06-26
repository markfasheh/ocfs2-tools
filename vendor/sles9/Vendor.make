#
# SLES 9
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/sles9/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = SLE9
SYSTEMD_ENABLED = 0

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
