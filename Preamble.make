ifeq ($(TOPDIR)/Config.make,$(wildcard $(TOPDIR)/Config.make))
include $(TOPDIR)/Config.make
else
.PHONY: dummy-notconfigured
dummy-notconfigured:
	@echo "Please run the configure script first"
endif

LIBRARIES =
PROGRAMS =
MODULES =
MANS =

INSTALL_RULES =

CLEAN_FILES =
CLEAN_RULES = 

DIST_FILES =
DIST_RULES =

INCLUDES =
DEFINES = 

CFLAGS += $($(subst /,_,$(basename $@))_CFLAGS)
CFLAGS += -pipe

LINK = $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

VERSION_FILES =
VERSION_PREFIX =
VERSION_SRC =

DIST_TOPDIR = $(TOPDIR)/$(PACKAGE)-$(DIST_VERSION)
DIST_CURDIR = .
DIST_DIR = $(DIST_TOPDIR)/$(DIST_CURDIR)

GZIP_OPTS = --best

.PHONY: all strip install
all: all-rules
strip: strip-rules
install: install-rules
