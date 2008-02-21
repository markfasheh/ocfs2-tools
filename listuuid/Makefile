TOPDIR = ..

include $(TOPDIR)/Preamble.make

INCLUDES = -I$(TOPDIR)/include

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

UNINST_PROGRAMS = listuuid

CFILES =	listuuid.c

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) 

listuuid: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(COM_ERR_LIBS) $(UUID_LIBS)

include $(TOPDIR)/Postamble.make
