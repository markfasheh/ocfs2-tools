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

toolbar_data = (
    ('Mount', gtk.STOCK_EXECUTE, 'mount'),
    ('Unmount', gtk.STOCK_STOP, 'unmount'),
    ('Refresh', gtk.STOCK_REFRESH, 'refresh')
)

class Toolbar:
    def __init__(self, **callbacks):
        self.callbacks = callbacks

    def get_widgets(self, data=None):
        toolbar = gtk.Toolbar()
        items = {}

        for i in toolbar_data:
            def make_cb():
                callback = self.callbacks[i[2]]

                def cb(w, d=None):
                    callback(d)

                return cb

            icon = gtk.Image()
            icon.set_from_stock(i[1], gtk.ICON_SIZE_BUTTON)
            items[i[2]] = toolbar.append_item(i[0], i[0], None, icon,
                                              make_cb(), data)

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

    cb = {}
    for i in toolbar_data:
        cb[i[2]] = dummy

    toolbar = Toolbar(**cb)

    window = gtk.Window()
    window.connect('delete_event', dummy)

    vbox = gtk.VBox()
    window.add(vbox)

    vbox.add(toolbar.get_widgets()[0])

    window.show_all()

    gtk.main()

if __name__ == '__main__':
    main()
