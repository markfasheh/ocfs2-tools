TOPDIR = ..

include $(TOPDIR)/Preamble.make

SBIN_EXTRA = load_ocfs

MANS = load_ocfs.8 ocfs.conf.5

INSTALL_RULES = install-sbin-links install-man-links

install-sbin-links: install-sbin-extra
	cd $(DESTDIR)$(sbindir) \
	&& rm -f load_ocfs2 \
	&& $(LN_S) load_ocfs load_ocfs2

install-man-links: install-mans
	cd $(DESTDIR)$(mandir)/man8 \
	&& rm -f load_ocfs2.8 \
        && $(LN_S) load_ocfs.8 load_ocfs2.8

DIST_FILES = load_ocfs.8.in ocfs.conf.5.in

include $(TOPDIR)/Postamble.make
