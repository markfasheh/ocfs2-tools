TOPDIR = ..

include $(TOPDIR)/Preamble.make

#WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
#           -Wmissing-declarations

ifdef OCFS2_DEBUG
OPTS = -O -ggdb
else
OPTS = -O2
endif

CFLAGS = $(OPTS) $(WARNINGS)

UNINST_PROGRAMS = sizetest.ocfs2

INCLUDES = -I$(TOPDIR)/include
DEFINES = -DVERSION=\"$(VERSION)\"

CFILES = sizetest.c
OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES)

sizetest.ocfs2: $(OBJS) $(LIBOCFS2_DEPS)
	$(LINK)

include $(TOPDIR)/Postamble.make
