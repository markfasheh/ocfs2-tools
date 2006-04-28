TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wmissing-declarations

ifdef OCFS2_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

INCLUDES = -Iinclude

LIBRARIES = libo2cb.a

CFLAGS = $(OPTS) $(WARNINGS) -fPIC
CPPFLAGS += -DO2CB_FLAT_INCLUDES

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
	o2cb_crc32.c

HFILES =				\
	include/ocfs2_nodemanager.h	\
	include/ocfs2_heartbeat.h	\
	include/o2cb_abi.h		\
	include/o2cb_crc32.h		\
	include/o2cb.h			\
	include/sparse_endian_types.h

HFILES_GEN =		\
	include/o2cb_err.h

HEADERS_SUBDIR = o2cb
HEADERS = $(HFILES) $(HFILES_GEN)

OBJS = $(subst .c,.o,$(CFILES)) \
	o2cb_err.o

$(OBJS): $(HFILES_GEN)

include/o2cb_err.h: o2cb_err.h
	cp $< $@

o2cb_err.c o2cb_err.h: o2cb_err.et
	compile_et o2cb_err.et

libo2cb.a: $(OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

DIST_FILES = $(CFILES) $(HFILES) o2cb_err.et

DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

CLEAN_RULES = clean-err

clean-err:
	rm -f o2cb_err.c o2cb_err.h include/o2cb_err.h

include $(TOPDIR)/Postamble.make
