TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format

ifneq ($(OCFS_PROCESSOR),x86_64)
WARNINGS += -Wmissing-prototypes -Wmissing-declarations
endif

ifdef OCFS_DEBUG
OPTS += -g
endif

DEFINES = -DLINUX -DUSERSPACE_TOOL

INCLUDES = -I. -ICommon/inc -ILinux/inc

LIBRARIES = libocfs.a

CFLAGS = $(OPTS) -fno-strict-aliasing -pthread $(WARNINGS)

OPTIMIZE = -O2

ifeq ($(OCFS_PROCESSOR),ppc64)
endif
ifeq ($(OCFS_PROCESSOR),x86_64)
  CFLAGS += -m64
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
  DEFINES += -D__ILP32__
endif

CFLAGS += $(OPTIMIZE)

ifdef OCFS_DEBUG
DEFINES += -DDEBUG
endif

ifdef OCFS_TRACE
DEFINES += -DTRACE
endif

CPARTNER = \
	Common/ocfsgenalloc.c	\
	Common/ocfsgensysfile.c	\
	Common/ocfsgencreate.c	\
	Common/ocfsgenutil.c	\
	Common/ocfsgenmisc.c	\
	Common/ocfsgentrans.c	\
	Common/ocfsgendirnode.c	\
	Common/ocfsgenvolcfg.c	\
	Linux/ocfsmount.c	\
	Linux/ocfsfile.c	\
	Linux/ocfsport.c	\
	Linux/ocfsbitmap.c	\
	Linux/ocfshash.c	\
	Common/ocfsgennm.c	\
	Common/ocfsheartbeat.c	\
	Common/ocfsgendlm.c 	\
	libocfs.c		\
	bindraw.c

CALONE = \
	Linux/ocfsdlm.c

CFILES = $(CPARTNER) $(CALONE)

HTEMP1 = $(subst .c,.h,$(CPARTNER))
HTEMP2 = $(subst Common,Common/inc,$(HTEMP1))
HPARTNER = $(subst Linux,Linux/inc,$(HTEMP2))

HALONE = \
	Common/inc/ocfs.h		\
	Common/inc/ocfsbool.h		\
	Common/inc/ocfsdef.h		\
	Common/inc/ocfserr.h		\
	Common/inc/ocfstrace.h		\
	Common/inc/ocfsvol.h		\
	Common/inc/ocfscom.h		\
	Common/inc/ocfsdisk.h		\
	Common/inc/ocfstrans.h		\
	Common/inc/ocfsconst.h		\
	Common/inc/ocfsdlm.h		\
	Common/inc/ocfsver.h		\
	Common/inc/ocfsgenvote.h	\
	Linux/inc/ocfsdlmp.h		\
	Linux/inc/ocfsioctl.h		\
	Linux/inc/ocfsiosup.h		\
	Linux/inc/ocfsmain.h		\
	Linux/inc/ocfsproc.h

HFILES = $(HPARTNER) $(HALONE)

$(CFILES): $(HFILES)

OBJS = $(subst .c,.o,$(CFILES))

libocfs.a: $(OBJS)
	rm -f $@
	$(AR) r $@ $^
	$(RANLIB) $@

CLEAN_RULES = clean-libocfs

clean-libocfs:
	rm -f Common/*.o Linux/*.o Common/*.p Linux/*.p Common/*.s Linux/*.s *.o *.p *.s

DIST_FILES = $(CFILES) $(HFILES)

DIST_RULES = dist-subdircreate

dist-subdircreate:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/Common/inc
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/Linux/inc

include $(TOPDIR)/Postamble.make
