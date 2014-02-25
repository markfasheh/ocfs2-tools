TOPDIR = ..

include $(TOPDIR)/Preamble.make

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm $(DL_LIBS)
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
ifneq ($(BUILD_CMAP_SUPPORT),)
LIBO2CB_LIBS += -lcmap
endif
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

LIBTOOLS_INTERNAL_LIBS = -L$(TOPDIR)/libtools-internal -ltools-internal
LIBTOOLS_INTERNAL_DEPS = $(TOPDIR)/libtools-internal/libtools-internal.a

sbindir = $(root_sbindir)
SBIN_PROGRAMS = mounted.ocfs2

INCLUDES = -I$(TOPDIR)/include

CFILES = mounted.c
OBJS = $(subst .c,.o,$(CFILES))

MANS = mounted.ocfs2.8

DIST_FILES = $(CFILES) mounted.ocfs2.8.in

mounted.ocfs2: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS) ${LIBTOOLS_INTERNAL_DEPS}
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS)  $(LIBO2CB_LIBS) ${LIBTOOLS_INTERNAL_DEPS} $(COM_ERR_LIBS) $(UUID_LIBS) $(AIO_LIBS)

include $(TOPDIR)/Postamble.make
