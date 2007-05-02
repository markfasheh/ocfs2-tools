#
# RHEL 5
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/rhel5/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = el5

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
