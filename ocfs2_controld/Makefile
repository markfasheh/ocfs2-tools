TOPDIR = ..

include $(TOPDIR)/Preamble.make

ifneq ($(BUILD_CMAN_SUPPORT),)
UNINST_PROGRAMS = test_client
endif

INCLUDES = -I$(TOPDIR)/include
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

TEST_CFILES = test_client.c

TEST_OBJS = $(subst .c,.o,$(TEST_CFILES) $(PROTO_CFILES))
MANS =

DIST_FILES =				\
	$(TEST_CFILES)			\
	$(addsuffix .in,$(MANS))

test_client: $(TEST_OBJS) $(LIBO2CB_DEPS) $(LIBOCFS2_DEPS)
	$(LINK) $(LIBOCFS2_LIBS) $(LIBO2CB_LIBS) $(COM_ERR_LIBS)

include $(TOPDIR)/Postamble.make
