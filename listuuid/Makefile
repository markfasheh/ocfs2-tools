TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = listuuid

INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include \
	    -I$(TOPDIR)/libocfs2cluster/include \
		-I$(TOPDIR)/libo2dlm/include \
		-I$(TOPDIR)/libo2cb/include

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

ifdef OCFS_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

CFLAGS := $(OPTS) -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

DEFINES = -DOCFS2_FLAT_INCLUDES -DO2DLM_FLAT_INCLUDES -DO2CB_FLAT_INCLUDES

CFILES =	listuuid.c

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) 
DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

listuuid: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(COM_ERR_LIBS) $(UUID_LIBS)

include $(TOPDIR)/Postamble.make
