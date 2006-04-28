ifeq ($(TOPDIR)/Config.make,$(wildcard $(TOPDIR)/Config.make))
include $(TOPDIR)/Config.make
else
.PHONY: dummy-notconfigured
dummy-notconfigured:
	@echo "Please run the configure script first"
endif

LIBRARIES =
UNINST_LIBRARIES =
BIN_PROGRAMS =
SBIN_PROGRAMS =
UNINST_PROGRAMS =
MODULES =
HEADERS =
MANS =

HEADERS_SUBDIR = 

INSTALL_RULES =

CLEAN_FILES =
CLEAN_RULES = 

DIST_FILES =
DIST_RULES =

INCLUDES =
DEFINES = 

CFLAGS += $($(subst /,_,$(basename $@))_CFLAGS)
CFLAGS += -pipe
# protect with configure?
CDEPFLAGS = -MD -MP -MF $(@D)/.$(basename $(@F)).d

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
