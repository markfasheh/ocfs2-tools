TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = mount.ocfs2

INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include \
	    -I$(TOPDIR)/libo2cb/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

ifdef OCFS_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

CFLAGS := $(OPTS) -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

DEFINES = -DOCFS2_FLAT_INCLUDES

CFILES =	mount.ocfs2.c

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) 
DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

mount.ocfs2: $(OBJS) $(LIBOCFS2_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

group: group.o $(LIBOCFS2_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
