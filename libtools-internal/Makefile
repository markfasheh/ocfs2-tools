TOPDIR = ..

include $(TOPDIR)/Preamble.make

INCLUDES = -I. -I$(TOPDIR)/include

UNINST_LIBRARIES = libtools-internal.a

CFLAGS += -fPIC

DEFINES += -DVERSION=\"$(VERSION)\"

ifneq ($(OCFS2_DEBUG_EXE),)
DEBUG_EXE_FILES = $(shell awk '/DEBUG_EXE/{if (k[FILENAME] == 0) {print FILENAME; k[FILENAME] = 1;}}' $(CFILES))
DEBUG_EXE_PROGRAMS = $(addprefix debug_,$(subst .c,,$(DEBUG_EXE_FILES)))

.SECONDARY:

UNINST_PROGRAMS += $(DEBUG_EXE_PROGRAMS)

debug_%.o : %.c 
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) \
		$(INCLUDES) $(DEFINES) \
		-DDEBUG_EXE -o $@ -c $<

debug_%: debug_%.o
	$(LINK) $(COM_ERR_LIBS)

endif

CFILES = verbose.c progress.c
HFILES = libtools-internal.h

OBJS = $(subst .c,.o,$(CFILES))

$(OBJS): $(HFILES) $(HFILES_GEN)

libtools-internal.a: $(OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

DIST_FILES = $(CFILES) $(HFILES)

include $(TOPDIR)/Postamble.make
