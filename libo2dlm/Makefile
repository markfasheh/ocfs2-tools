TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wmissing-prototypes \
	-Wmissing-declarations

ifdef OCFS_DEBUG
OPTS += -ggdb
else
OPTS += -O2
endif

INCLUDES = -Iinclude

LIBRARIES = libo2dlm.a

CFLAGS = $(OPTS) $(WARNINGS) -fPIC
CPPFLAGS += -DO2DLM_FLAT_INCLUDES

ifneq ($(OCFS2_DEBUG_EXE),)
DEBUG_EXE_FILES = $(shell awk '/DEBUG_EXE/{if (k[FILENAME] == 0) {print FILENAME; k[FILENAME] = 1;}}' $(CFILES))
DEBUG_EXE_PROGRAMS = $(addprefix debug_,$(subst .c,,$(DEBUG_EXE_FILES)))

.SECONDARY:

UNINST_PROGRAMS += $(DEBUG_EXE_PROGRAMS)

debug_%.o : %.c 
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) \
		$(INCLUDES) $(DEFINES) \
		-DDEBUG_EXE -o $@ -c $<

debug_%: debug_%.o libo2dlm.a
	$(LINK) $(COM_ERR_LIBS)

endif

CFILES = o2dlm.c

TESTING_CFILES = o2dlm_test.c

HFILES = 			\
	include/kernel-list.h	\
	include/o2dlm.h 

HFILES_GEN =			\
	include/o2dlm_err.h

OBJS = $(subst .c,.o,$(CFILES)) \
	o2dlm_err.o

$(OBJS): $(HFILES_GEN)

TESTING_OBJS = $(subst .c,.o,$(TESTING_CFILES))

include/o2dlm_err.h: o2dlm_err.h
	cp $< $@

o2dlm_err.c o2dlm_err.h: o2dlm_err.et
	compile_et o2dlm_err.et

libo2dlm.a: $(OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

o2dlm_test: $(TESTING_OBJS) $(LIBRARIES) $(COM_ERR_LIBS)
	$(LINK)

DIST_FILES = $(CFILES) $(HFILES) o2dlm_err.et

DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/include

CLEAN_RULES = clean-err clean-test

clean-err:
	rm -f o2dlm_err.c o2dlm_err.h include/o2dlm_err.h

clean-test:
	rm -f o2dlm_test

include $(TOPDIR)/Postamble.make
