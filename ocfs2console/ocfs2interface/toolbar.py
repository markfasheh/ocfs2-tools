# OCFS2Console - GUI frontend for OCFS2 management and debugging
# Copyright (C) 2005 Oracle.  All rights reserved.
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

from guiutil import make_callback

toolbar_data = (
    ('Mount',   gtk.STOCK_EXECUTE, 'mount',   'refresh'),
    ('Unmount', gtk.STOCK_STOP,    'unmount', 'refresh'),
    ('Refresh', gtk.STOCK_REFRESH, 'refresh',  None)
)

class Toolbar:
    def __init__(self, window):
        self.window = window

    def get_widgets(self):
        toolbar = gtk.Toolbar()
        items = {}

        for data in toolbar_data:
            label, stock_id, callback, sub_callback = data

            cb = make_callback(self.window, callback, sub_callback)

            icon = gtk.Image()
            icon.set_from_stock(stock_id, gtk.ICON_SIZE_BUTTON)
            items[callback] = toolbar.append_item(label, label, None, icon, cb)

        toolbar.append_space()

        filter_box, entry = self.get_filter_box()
        toolbar.append_widget(filter_box, 'Partition name filter', None)

        return toolbar, items, entry

    def get_filter_box(self):
        hbox = gtk.HBox(False, 4)

        label = gtk.Label('Filter:')
        hbox.pack_start(label, expand=False, fill=False)

        entry = gtk.Entry()
        hbox.pack_end(entry)

        return hbox, entry

def main():
    def dummy(*args):
        gtk.main_quit()

    window = gtk.Window()
    window.connect('delete_event', dummy)

    for i in toolbar_data:
        setattr(window, i[2], dummy)

    toolbar = Toolbar(window)

    vbox = gtk.VBox()
    window.add(vbox)

    vbox.add(toolbar.get_widgets()[0])

    window.show_all()

    gtk.main()

if __name__ == '__main__':
    main()
