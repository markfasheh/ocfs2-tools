TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_EXTRA = o2hbmonitor

WARNINGS = -Wall -Wstrict-prototypes -Wno-format -Wmissing-prototypes \
           -Wmissing-declarations

CFLAGS = $(OPTS) $(WARNINGS)

INCLUDES = -I$(TOPDIR)/include -I.

DEFINES = -DVERSION=\"$(VERSION)\"

CFILES = o2hbmonitor.c

OBJS = $(subst .c,.o,$(CFILES))

DIST_FILES = $(CFILES) $(HFILES)

o2hbmonitor: $(OBJS)
	$(LINK)

include $(TOPDIR)/Postamble.make
