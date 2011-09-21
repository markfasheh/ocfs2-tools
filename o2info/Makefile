TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

CFLAGS += $(WARNINGS)

LIBTOOLS_INTERNAL_LIBS = -L$(TOPDIR)/libtools-internal -ltools-internal
LIBTOOLS_INTERNAL_DEPS = $(TOPDIR)/libtools-internal/libtools-internal.a

LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2
LIBOCFS2_DEPS = $(TOPDIR)/libocfs2/libocfs2.a

BIN_PROGRAMS = o2info

INCLUDES = -I$(TOPDIR)/include -I.
DEFINES = -DVERSION=\"$(VERSION)\"

MANS = o2info.1

HFILES = o2info.h		\
	 utils.h		\
	 libo2info.h

CFILES =			\
	o2info.c		\
	operations.c		\
	utils.c

LIBO2INFO_CFILES = libo2info.c

OBJS = $(subst .c,.o,$(CFILES))

LIBO2INFO_OBJS = $(subst .c,.o,$(LIBO2INFO_CFILES))

DIST_FILES = $(CFILES) $(LIBO2INFO_CFILES) $(HFILES) o2info.1.in

libo2info.a: $(LIBO2INFO_OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

o2info: $(OBJS) $(LIBOCFS2_DEPS) libo2info.a
	$(LINK) $(LIBOCFS2_LIBS) $(LIBTOOLS_INTERNAL_LIBS) $(COM_ERR_LIBS) libo2info.a

include $(TOPDIR)/Postamble.make
