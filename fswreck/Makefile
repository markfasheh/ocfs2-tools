TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = fswreck

DEFINES = -DG_DISABLE_DEPRECATED -DLINUX
DEFINES += -DOCFS2_FLAT_INCLUDES -DO2DLM_FLAT_INCLUDES -DO2CB_FLAT_INCLUDES -DVERSION=\"$(VERSION)\"

INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include -I$(TOPDIR)/libo2dlm/include -I$(TOPDIR)/libo2cb/include
INCLUDES += $(GLIB_CFLAGS)

ifdef OCFS_DEBUG
CFLAGS = -Wall -O -ggdb
else
CFLAGS = -Wall -O2
endif

CFILES = main.c corrupt.c chain.c

HFILES = 			\
	include/main.h		\
	include/corrupt.h	\
	include/chain.c

OBJS = $(subst .c,.o,$(CFILES))

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

fswreck: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(GLIB_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
