TOPDIR = ..

include $(TOPDIR)/Preamble.make

sbindir = $(root_sbindir)

ifneq ($(BUILD_OCFS2_CONTROLD),)
  ifneq ($(BUILD_CMAN_SUPPORT),)
SBIN_PROGRAMS = ocfs2_controld.cman
  endif
UNINST_PROGRAMS = test_client
endif

INCLUDES = -I$(TOPDIR)/include -I.
LIBO2CB_LIBS = -L$(TOPDIR)/libo2cb -lo2cb
LIBO2CB_DEPS = $(TOPDIR)/libo2cb/libo2cb.a
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a
OPENAIS_LIBS = $(CPG_LDFLAGS) -lcpg -lSaCkpt

ifdef OCFS2_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

CFLAGS := $(OPTS) -Wall -Wstrict-prototypes -Wmissing-prototypes \
	  -Wmissing-declarations

DEFINES = -DOCFS2_FLAT_INCLUDES -DO2DLM_FLAT_INCLUDES \
	  -DO2CB_FLAT_INCLUDES -DHAVE_CMAN -DVERSION=\"$(VERSION)\"

UNINST_HFILES = ocfs2_controld.h

DAEMON_CFILES = main.c cpg.c mount.c ckpt.c

CMAN_CFILES = cman.c
CMAN_DAEMON_CFILES = $(DAEMON_CFILES) $(CMAN_CFILES)
CMAN_DAEMON_OBJS = $(subst .c,.o,$(CMAN_DAEMON_CFILES))

TEST_CFILES = test_client.c
TEST_OBJS = $(subst .c,.o,$(TEST_CFILES) $(PROTO_CFILES))
MANS =

DIST_FILES =				\
	$(DAEMON_CFILES)		\
	$(CMAN_CFILES)			\
	$(TEST_CFILES)			\
	$(UNINST_HFILES)		\
	$(addsuffix .in,$(MANS))

ocfs2_controld.cman: $(CMAN_DAEMON_OBJS) $(LIBO2CB_DEPS)
	$(LINK) $(LIBO2CB_LIBS) $(COM_ERR_LIBS) $(OPENAIS_LIBS) -lcman

test_client: $(TEST_OBJS) $(LIBO2CB_DEPS) $(LIBOCFS2_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
