#
# Fedora 7
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/fc7/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = fc7

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
