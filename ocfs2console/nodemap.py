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

COLUMN_SLOT = 0
COLUMN_NAME = 1
COLUMN_IP = 2
COLUMN_PORT = 3
COLUMN_UUID = 4

class NodeMap:
    def __init__(self, device=None):
        self.device = device

        info = self.info()

        if info:
            self.widget = gtk.ScrolledWindow()
            self.widget.set_policy(gtk.POLICY_AUTOMATIC, gtk.POLICY_AUTOMATIC)
            self.widget.add(info)
        else:
            self.widget = gtk.Label('Invalid device')

    def info(self):
        if not self.device:
            return None

        store = gtk.ListStore(int, str, str, int, str)

        tv = gtk.TreeView(store)

        tv.insert_column_with_attributes(-1, 'Slot #',
                                         gtk.CellRendererText(),
                                         text=COLUMN_SLOT)
        tv.insert_column_with_attributes(-1, 'Node Name',
                                         gtk.CellRendererText(),
                                         text=COLUMN_NAME)
        tv.insert_column_with_attributes(-1, 'IP Address',
                                         gtk.CellRendererText(),
                                         text=COLUMN_IP)
        tv.insert_column_with_attributes(-1, 'Port',
                                         gtk.CellRendererText(),
                                         text=COLUMN_PORT)
        tv.insert_column_with_attributes(-1, 'UUID',
                                         gtk.CellRendererText(),
                                         text=COLUMN_UUID)

        return tv

def main():
    import sys

    def dummy(*args):
        gtk.main_quit()

    window = gtk.Window()
    window.connect('delete_event', dummy)

    nodemap = NodeMap(sys.argv[1]).widget
    window.add(nodemap)

    window.show_all()

    gtk.main()

if __name__ == '__main__':
    main()
