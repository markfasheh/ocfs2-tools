TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_PROGRAMS = ocfs2_controld
UNINST_PROGRAMS = test_client

#LIBO2DLM_LIBS = -L$(TOPDIR)/libo2dlm -lo2dlm
#LIBO2DLM_DEPS = $(TOPDIR)/libo2dlm/libo2dlm.a

INCLUDES = -I$(TOPDIR)/libocfs2/include -I$(TOPDIR)/libo2dlm/include -I$(TOPDIR)/libo2cb/include
LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

ifdef OCFS2_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

CFLAGS := $(OPTS) -Wall -Wstrict-prototypes -Wmissing-prototypes \
           -Wmissing-declarations

DEFINES = -DOCFS2_FLAT_INCLUDES -DO2DLM_FLAT_INCLUDES \
	  -DO2CB_FLAT_INCLUDES -DHAVE_CMAN -DVERSION=\"$(VERSION)\"

DAEMON_CFILES = group.c main.c member_cman.c action.c


UNINST_HFILES = ocfs2_controld_internal.h

TEST_CFILES = test_client.c

DAEMON_OBJS = $(subst .c,.o,$(DAEMON_CFILES))
TEST_OBJS = $(subst .c,.o,$(TEST_CFILES) $(PROTO_CFILES))
MANS =

DIST_FILES =				\
	$(DAEMON_CFILES)		\
	$(TEST_CFILES)			\
	$(UNINST_HFILES)		\
	$(addsuffix .in,$(MANS))

ocfs2_controld: $(DAEMON_OBJS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBO2CB_LIBS) $(COM_ERR_LIBS) -lgroup -lcman

test_client: $(TEST_OBJS) $(LIBO2CB_DEPS) $(LIBOCFS2_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
