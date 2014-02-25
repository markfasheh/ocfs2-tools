TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wmissing-declarations

INCLUDES = -I$(TOPDIR)/include -I.

LIBRARIES = libo2cb.a

CFLAGS += -fPIC

ifneq ($(BUILD_CMAP_SUPPORT),)
DEFINES += -DHAVE_CMAP
endif

ifneq ($(BUILD_FSDLM_SUPPORT),)
DEFINES += -DHAVE_FSDLM
endif

ifneq ($(OCFS2_DEBUG_EXE),)
DEBUG_EXE_FILES = $(shell awk '/DEBUG_EXE/{if (k[FILENAME] == 0) {print FILENAME; k[FILENAME] = 1;}}' $(CFILES))
DEBUG_EXE_PROGRAMS = $(addprefix debug_,$(subst .c,,$(DEBUG_EXE_FILES)))

.SECONDARY:

UNINST_PROGRAMS += $(DEBUG_EXE_PROGRAMS)

debug_%.o : %.c 
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) \
		$(INCLUDES) $(DEFINES) \
		-DDEBUG_EXE -o $@ -c $<

debug_%: debug_%.o libo2cb.a
	$(LINK) $(COM_ERR_LIBS)

endif

CFILES = 		\
	o2cb_abi.c	\
	o2cb_crc32.c	\
	client_proto.c

HFILES =			\
	o2cb_abi.h		\
	o2cb_crc32.h

HFILES_GEN = o2cb_err.h

OBJS = $(subst .c,.o,$(CFILES)) \
	o2cb_err.o

ifneq ($(BUILD_CMAN_SUPPORT),)
DEFINES += -DHAVE_CMAN
endif

$(OBJS): $(HFILES_GEN)

o2cb_err.c o2cb_err.h: o2cb_err.et
	compile_et o2cb_err.et

libo2cb.a: $(OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

MANS = o2cb.7

DIST_FILES = $(CFILES) $(HFILES) o2cb_err.et o2cb.7.in

CLEAN_RULES = clean-err

clean-err:
	rm -f o2cb_err.c o2cb_err.h

include $(TOPDIR)/Postamble.make
