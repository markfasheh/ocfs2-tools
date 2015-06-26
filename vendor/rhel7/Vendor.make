#
# RHEL 7
#

TOOLSARCH = $(shell $(TOPDIR)/vendor/rhel7/rpmarch.guess tools $(TOPDIR))
VENDOR_EXTENSION = el7
SYSTEMD_ENABLED = 1

include $(TOPDIR)/vendor/common/Vendor.make

packages: rpm
