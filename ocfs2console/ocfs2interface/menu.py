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

try:
    stock_about = gtk.STOCK_ABOUT
except AttributeError:
    stock_about = ''

menu_data = (
    ('/_File',                     None,         None,      0, '<Branch>'),
    ('/File/_Quit',                None,         'cleanup', 0, '<StockItem>',
     gtk.STOCK_QUIT),
    ('/_Tasks',                    None,         None,      0, '<Branch>'),
    ('/Tasks/_Format...',          '<control>F', 'format'),
    ('/Tasks/---',                 None,         None,      0, '<Separator>'),
    ('/Tasks/Chec_k...',           '<control>K', 'check'),
    ('/Tasks/_Repair...',          '<control>R', 'repair'),
    ('/Tasks/---',                 None,         None,      0, '<Separator>'),
    ('/Tasks/_Cluster Config...',  '<control>C', 'clconfig'),
    ('/_Help',                     None,         None,      0, '<Branch>'),
    ('/Help/_About',               None,         'about',   0, '<StockItem>',
     stock_about)
)

class Menu:
    def __init__(self, **callbacks):
        self.items = []

        for i in menu_data:
            item = list(i)

            if i[2]:
                def make_cb():
                    callback = callbacks[i[2]]

                    def cb(d, a, w):
                        callback(d)

                    return cb

                item[2] = make_cb()

            self.items.append(tuple(item))

    def get_widget(self, window, data=None):
        accel_group = gtk.AccelGroup()
        window.add_accel_group(accel_group)

        item_factory = gtk.ItemFactory(gtk.MenuBar, '<main>', accel_group)
        item_factory.create_items(self.items, data)

        window.item_factory = item_factory

        return item_factory.get_widget('<main>')

def main():
    def dummy(*args):
        gtk.main_quit()

    cb = {}
    for i in menu_data:
        if i[2]:
            cb[i[2]] = dummy

    menubar = Menu(**cb)

    window = gtk.Window()
    window.connect('delete_event', dummy)

    vbox = gtk.VBox()
    window.add(vbox)

    vbox.add(menubar.get_widget(window))

    window.show_all()

    gtk.main()

if __name__ == '__main__':
    main()
