TOPDIR = ..

include $(TOPDIR)/Preamble.make

sbindir = $(root_sbindir)
SBIN_PROGRAMS = ocfs2_hb_ctl

INCLUDES = -I$(TOPDIR)/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2 -laio
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a
LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm $(DL_LIBS)
LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a
LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

ifndef OCFS2_DYNAMIC_CTL
LDFLAGS += -static
endif

DEFINES = -DVERSION=\"$(VERSION)\"

CFILES = ocfs2_hb_ctl.c

HFILES =

OBJS = $(subst .c,.o,$(CFILES))

MANS = ocfs2_hb_ctl.8

DIST_FILES = $(CFILES) $(HFILES) ocfs2_hb_ctl.8.in

all: ocfs2_hb_ctl

ocfs2_hb_ctl: $(OBJS) $(LIBOCFS2_DEPS) $(LIBO2DLM_DEPS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2DLM_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
