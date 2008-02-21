TOPDIR = ..

include $(TOPDIR)/Preamble.make

UNINST_PROGRAMS = sizetest.ocfs2

INCLUDES = -I$(TOPDIR)/include
DEFINES = -DVERSION=\"$(VERSION)\"

CFILES = sizetest.c
OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES)

sizetest.ocfs2: $(OBJS) $(LIBOCFS2_DEPS)
	$(LINK)

include $(TOPDIR)/Postamble.make
