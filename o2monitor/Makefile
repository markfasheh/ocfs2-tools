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

MANS = o2hbmonitor.8

DIST_FILES = $(CFILES) $(HFILES) o2hbmonitor.8.in

o2hbmonitor: $(OBJS)
	$(LINK)

include $(TOPDIR)/Postamble.make
