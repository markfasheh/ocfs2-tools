TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) $(WARNINGS) 

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2

SBIN_PROGRAMS = mounted.ocfs2

INCLUDES = -I$(TOPDIR)/libocfs2/include

OPTIMIZE = -O2

CFLAGS += $(OPTIMIZE)

CFILES = mounted.c
OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES)

mounted.ocfs2: $(OBJS)
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
