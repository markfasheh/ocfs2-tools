TOPDIR = ..

include $(TOPDIR)/Preamble.make

sbindir = $(root_sbindir)
SBIN_PROGRAMS = o2cb_controld

#INCLUDES = -Iinclude -I$(TOPDIR)/libocfs2/include \
#	    -I$(TOPDIR)/libo2cb/include -I$(TOPDIR)/libo2dlm/include
#LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
#LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a
#LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
#LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

INCLUDES = -I$(TOPDIR)/libo2dlm/include -I$(TOPDIR)/libo2cb/include
LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a

ifdef OCFS2_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

CFLAGS := $(OPTS) -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

DEFINES = -DO2CB_FLAT_INCLUDES -DHAVE_CMAN -DVERSION=\"$(VERSION)\"

CFILES = main.c action.c member_cman.c

HFILES = o2cb_controld.h

OBJS = $(subst .c,.o,$(CFILES))
#MANS = ocfs2_dlm_controld

DIST_FILES = $(CFILES) $(HFILES) $(addsuffix .in,$(MANS))

o2cb_controld: $(OBJS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBO2CB_LIBS) $(COM_ERR_LIBS) -lcman

include $(TOPDIR)/Postamble.make
