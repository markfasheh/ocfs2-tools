# OCFS2Console - GUI frontend for OCFS2 management and debugging
# Copyright (C) 2002, 2005 Oracle.  All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 021110-1307, USA.

import gtk

import ocfs2

from classlabel import class_label
from guiutil import set_props, format_bytes

EMPTY_TEXT = 'N/A'

class Field(object):
    def __init__(self, fs, super, dinode):
        self.fs = fs
        self.super = super
        self.dinode = dinode

    def get_text(self):
        if self.super:
            return self.real_get_text()
        else:
            return EMPTY_TEXT

    text = property(get_text)

    label = class_label

class Version(Field):
    def real_get_text(self):
        return '%d.%d' % (self.super.s_major_rev_level,
                          self.super.s_minor_rev_level)

class Label(Field):
    def real_get_text(self):
        text = self.super.s_label

        if not text:
            text = EMPTY_TEXT

        return text

class UUID(Field):
    def real_get_text(self):
        return self.super.uuid_unparsed

class MaximumNodes(Field):
    def real_get_text(self):
        return str(self.super.s_max_nodes)

class FSSize(Field):
    def real_get_text(self):
        return format_bytes(getattr(self.fs, self.member))

class ClusterSize(FSSize):
    member = 'fs_clustersize'

class BlockSize(FSSize):
    member = 'fs_blocksize'

class Space(Field):
    def real_get_text(self):
        if self.dinode:
            block_bits = self.fs.fs_clustersize >> self.super.s_blocksize_bits
            bytes = self.get_bits() * block_bits * self.fs.fs_blocksize
            return format_bytes(bytes, show_bytes=True)
        else:
            return EMPTY_TEXT

class FreeSpace(Space):
    def get_bits(self):
        return self.dinode.i_total - self.dinode.i_used

class TotalSpace(Space):
    def get_bits(self):
        return self.dinode.i_total

fields = (Version, Label, UUID, MaximumNodes,
          ClusterSize, BlockSize,
          FreeSpace, TotalSpace)

class General(gtk.Table):
    def __init__(self, device=None):
        gtk.Table.__init__(self, rows=5, columns=2)

        set_props(self, row_spacing=4,
                        column_spacing=4,
                        border_width=4)

        fs = super = dinode = None

        if device:
            try:
                fs = ocfs2.Filesystem(device)
                super = fs.fs_super

                blkno = fs.lookup_system_inode(ocfs2.GLOBAL_BITMAP_SYSTEM_INODE)
                dinode = fs.read_cached_inode(blkno)
            except ocfs2.error:
                pass

        for row, field_type in enumerate(fields):
            field = field_type(fs, super, dinode)

            label = gtk.Label(field.label + ':')
            set_props(label, xalign=1.0)
            self.attach(label, 0, 1, row, row + 1,
                        xoptions=gtk.FILL, yoptions=gtk.FILL)

            label = gtk.Label(field.text)
            set_props(label, xalign=0.0)
            self.attach(label, 1, 2, row, row + 1,
                        xoptions=gtk.FILL, yoptions=gtk.FILL)

def main():
    import sys

    def dummy(*args):
        gtk.main_quit()

    window = gtk.Window()
    window.connect('delete_event', dummy)

    general = General(sys.argv[1])
    window.add(general)

    window.show_all()

    gtk.main()

if __name__ == '__main__':
    main()
