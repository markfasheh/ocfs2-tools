# OCFS2Tool - GUI frontend for OCFS2 management and debugging
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

from guiutil import set_props, format_bytes

fields = (
    ('Version', 'version'),
    ('Label', 's_label'),
    ('UUID', 's_uuid'),
    ('Maximum Nodes', 's_max_nodes'),
    ('Cluster Size', 's_clustersize_bits'),
    ('Block Size', 's_blocksize_bits')
)

class General:
    def __init__(self, device=None):
        self.widget = gtk.Table(rows=5, columns=2)

        set_props(self.widget, row_spacing=4,
                               column_spacing=4,
                               border_width=4)

        super = None

        if device:
            try:
                super = ocfs2.get_super(device)
            except ocfs2.error:
                pass

        self.pos = 0

        for desc, member in fields:
            if super:
                if member == 'version':
                    val = '%d.%d' % super[0:2]
                elif member == 's_label':
                    val = super.s_label
                    if not val:
                        val = 'N/A'
                else:
                    val = getattr(super, member)

                    if member.endswith('_bits'):
                        val = format_bytes(1 << val)
            else:
                val = 'N/A'

            self.add_field(desc, val)

    def add_field(self, desc, val):
        label = gtk.Label(desc + ':')
        set_props(label, xalign=1.0)
        self.widget.attach(label, 0, 1, self.pos, self.pos + 1,
                           xoptions=gtk.FILL, yoptions=gtk.FILL)

        label = gtk.Label(str(val))
        set_props(label, xalign=0.0)
        self.widget.attach(label, 1, 2, self.pos, self.pos + 1,
                           xoptions=gtk.FILL, yoptions=gtk.FILL)

        self.pos += 1

def main():
    import sys

    def dummy(*args):
        gtk.main_quit()

    window = gtk.Window()
    window.connect('delete_event', dummy)

    general = General(sys.argv[1]).widget
    window.add(general)

    window.show_all()

    gtk.main()

if __name__ == '__main__':
    main()
