TOPDIR = ..

include $(TOPDIR)/Preamble.make

WARNINGS = -Wall

ifdef OCFS_DEBUG
OPTS = -g
endif
OPTS = -g

CFLAGS = $(OPTS) $(WARNINGS) -fPIC

LIBOCFS2_CFLAGS = -I$(TOPDIR)/libocfs2/include
LIBOCFS2_LIBS = -L$(TOPDIR)/libocfs2 -locfs2

DEFINES = -DOCFS2_FLAT_INCLUDES -DG_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGDK_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED
INCLUDES = $(LIBOCFS2_CFLAGS) $(PYTHON_INCLUDES) $(PYGTK_CFLAGS) $(GTK_CFLAGS)

OPTIMIZE = -O2
OPTIMIZE = -O0

CFLAGS += $(OPTIMIZE)

CBITS = \
	ocfsbitmap.c \
	ocfscellmap.c \
	ocfsplist.c

HBITS = $(subst .c,.h,$(CBITS))

CFILES = $(CBITS) ocfs2module.c
HFILES = $(HBITS)

ALL = $(CFILES) ocfsmarshal.c cellmap.c

OBJS = $(subst .c,.o,$(ALL))

MANS = ocfs2tool.8

SBIN_EXTRA = ocfs2tool

LIBRARIES = ocfs2module.so

PYLIB = $(LIBRARIES) main.py guiutil.py process.py general.py nodemap.py \
	bitmap.py browser.py format.py

INSTALL_RULES = install-pylib

CLEAN_RULES = gen-clean

DIST_FILES = $(CFILES) $(HFILES) ocfs2tool ocfsmarshal.list \
	cellmap.defs cellmap.override ocfs2tool.8.in

ocfsmarshal.h: ocfsmarshal.list
	$(GLIB_GENMARSHAL) --prefix=_ocfs_marshal ocfsmarshal.list --header >> xgen-omh \
	&& (cmp -s xgen-omh $(@F) || cp xgen-omh $(@F)) \
	&& rm -f xgen-omh xgen-omh~

ocfsmarshal.c: ocfsmarshal.list
	echo "#include \"ocfsmarshal.h\"" >> xgen-omc \
	&& $(GLIB_GENMARSHAL) --prefix=_ocfs_marshal ocfsmarshal.list --body >> xgen-omc \
	&& cp xgen-omc $(@F) \
	&& rm -f xgen-omc xgen-omc~

ocfscellmap.o: ocfscellmap.c ocfsmarshal.h
	$(CC) $(CFLAGS) $(LOCAL_CFLAGS) $(CPPFLAGS) $(LOCAL_CPPFLAGS) $(INCLUDES) $(DEFINES) -o $@ -c $<

cellmap.c: cellmap.defs cellmap.override
	$(PYGTK_CODEGEN) --prefix cellmap \
	--register $(PYGTK_DEFSDIR)/gdk-types.defs \
	--register $(PYGTK_DEFSDIR)/gtk-types.defs \
	--override cellmap.override \
	cellmap.defs > $@

ocfs2module.so: $(OBJS)
	$(LINK) -shared $(LIBOCFS2_LIBS) $(COM_ERR_LIBS) $(GTK_LIBS)

install-pylib:
	$(SHELL) $(TOPDIR)/mkinstalldirs $(DESTDIR)$(pyexecdir)/ocfs2tool
	for f in $(PYLIB); do \
	  $(INSTALL_DATA) $$f $(DESTDIR)$(pyexecdir)/ocfs2tool/$$f; \
	done

gen-clean:
	rm -f ocfsmarshal.c ocfsmarshal.h cellmap.c

include $(TOPDIR)/Postamble.make
