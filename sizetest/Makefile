TOPDIR = ..

include $(TOPDIR)/Preamble.make

#WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
#           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -O -ggdb
else
OPTS = -O2
endif

CFLAGS = $(OPTS) $(WARNINGS)

SBIN_PROGRAMS = sizetest.ocfs2

INCLUDES = -I$(TOPDIR)/libocfs2/include -I$(TOPDIR)/libo2dlm/include
DEFINES = -DOCFS2_FLAT_INCLUDES -DVERSION=\"$(VERSION)\" -DO2DLM_FLAT_INCLUDES

CFILES = sizetest.c
OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES)

sizetest.ocfs2: $(OBJS) $(LIBOCFS2_DEPS)
	$(LINK)

include $(TOPDIR)/Postamble.make
