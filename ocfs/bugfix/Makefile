TOPDIR = ../..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

ifdef OCFS_DEBUG
OPTS = -g
endif

CFLAGS = $(OPTS) -fno-strict-aliasing $(WARNINGS) -pthread

DIST_FILES = bug3123542.c bug3016598.c bug3123542-README.txt bug3016598-README.txt

SBIN_PROGRAMS = bug3016598 bug3123542

INCLUDES = -I$(TOPDIR)/ocfs/libocfs/Common/inc -I$(TOPDIR)/ocfs/libocfs/Linux/inc -Iinc -I$(TOPDIR)/ocfs/libocfs
DEFINES = -DLINUX -DUSERSPACE_TOOL

OPTIMIZE = -O2

ifeq ($(OCFS_PROCESSOR),ppc64)
endif
ifeq ($(OCFS_PROCESSOR),x86_64)
endif
ifeq ($(OCFS_PROCESSOR),ia64)
endif
ifeq ($(OCFS_PROCESSOR),i686)
  DEFINES += -D__ILP32__
endif
ifeq ($(OCFS_PROCESSOR),i586)
  DEFINES += -D__ILP32__
endif
ifeq ($(OCFS_PROCESSOR),s390x)
  DEFINES += -D__LP64__
endif

CFLAGS += $(OPTIMIZE)

ifdef OCFS_DEBUG
DEFINES += -DDEBUG
endif
ifdef OCFS_TRACE
DEFINES += -DTRACE
endif


VERSION_FILES = generic.c ver.c
VERSION_SRC = ver.c
VERSION_PREFIX = BUGFIX

DIST_RULES = dist-incdir

bug3016598: bug3016598.o ver.o generic.o
	$(LINK) -L$(TOPDIR)/ocfs/libocfs -locfs

bug3123542: bug3123542.o ver.o generic.o
	$(LINK) -L$(TOPDIR)/ocfs/libocfs -locfs

dist-incdir:
	$(TOPDIR)/mkinstalldirs $(DIST_DIR)/inc

include $(TOPDIR)/Postamble.make
