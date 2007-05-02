
#
# This Makefile snippet is to be included in Makebo Makefiles that
# use the mbvendor infrastructure
#

PKG_VERSION = $(shell $(TOPDIR)/svnrev.guess $(PACKAGE))

-include $(TOPDIR)/vendor/$(shell ./vendor.guess)/Vendor.make

