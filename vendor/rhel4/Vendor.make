#
# RHEL 4
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/rhel4/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = el4

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
