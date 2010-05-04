#
# RHEL 6
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/rhel6/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = el6

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
