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

from fsck import fsck_ok

UNMOUNTED_ONLY = 42
NEED_SELECTION = 43

try:
    stock_about = gtk.STOCK_ABOUT
except AttributeError:
    stock_about = ''

file_menu_data = (
    ('/_File',                     None,         None,       0, '<Branch>'),
    ('/File/_Quit',                None,         'cleanup',  0, '<StockItem>',
     gtk.STOCK_QUIT)
)

help_menu_data = (
    ('/_Help',                     None,         None,       0, '<Branch>'),
    ('/Help/_About',               None,         'about',    0, '<StockItem>',
     stock_about)
)

if fsck_ok:
    task_menu_fsck_data = (
        ('/Tasks/Chec_k...',       '<control>K', 'check',    'refresh',
         NEED_SELECTION),
        ('/Tasks/_Repair...',      '<control>R', 'repair',   'refresh',
         UNMOUNTED_ONLY),
        ('/Tasks/---',             None,         None,       0, '<Separator>')
    )
else:
    task_menu_fsck_data = ()

task_menu_head_data = (
    ('/_Tasks',                    None,         None,       0, '<Branch>'),
    ('/Tasks/_Format...',          '<control>F', 'format',   'refresh'),
    ('/Tasks/---',                 None,         None,       0, '<Separator>')
)

task_menu_tail_data = (
    ('/Tasks/Change _Label...',    None,         'relabel',  'refresh',
     UNMOUNTED_ONLY),
    ('/Tasks/Edit _Node Count...', None,         'node_num', 'refresh',
     UNMOUNTED_ONLY),
    ('/Tasks/---',                 None,         None,       0, '<Separator>'),
    ('/Tasks/_Cluster Config...',  None,         'clconfig')
)

task_menu_data = task_menu_head_data + task_menu_fsck_data + task_menu_tail_data

menu_data = file_menu_data + task_menu_data + help_menu_data

class Menu:
    def __init__(self, window):
        self.window = window

        self.items = []

        for data in menu_data:
            item = list(data)

            data_list = [None] * 6
            data_list[0:len(data)] = data

            path, accel, callback, sub_callback, item_type, extra = data_list

            if self.is_special(item_type):
                del item[4:]

            if callback:
                def make_cb():
                    cb = getattr(window, callback)

                    if sub_callback:
                        del item[3:]

                        sub_cb = getattr(window, sub_callback)

                        def cb_func(a, w):
                            cb()
                            sub_cb()
                    else:
                        def cb_func(a, w):
                            cb()

                    return cb_func

                item[2] = make_cb()

            self.items.append(tuple(item))

    def get_widgets(self):
        accel_group = gtk.AccelGroup()
        self.window.add_accel_group(accel_group)

        item_factory = gtk.ItemFactory(gtk.MenuBar, '<main>', accel_group)
        item_factory.create_items(self.items)

        menubar = item_factory.get_widget('<main>')

        self.unmounted_widgets = []
        self.need_sel_widgets = []
        
        for data in menu_data:
            if len(data) >= 5 and self.is_special(data[4]):
                path = data[0].replace('_', '')
                menuitem = item_factory.get_item('<main>%s' % path)

                widget_list = self.get_special_list(data[4])
                widget_list.append(menuitem)

        self.window.item_factory = item_factory
                  
        return menubar, self.need_sel_widgets, self.unmounted_widgets

    def is_special(self, data):
        return data in (UNMOUNTED_ONLY, NEED_SELECTION)

    def get_special_list(self, data):
        if data == UNMOUNTED_ONLY:
            return self.unmounted_widgets
        elif data == NEED_SELECTION:
            return self.need_sel_widgets

def main():
    def dummy(*args):
        gtk.main_quit()

    window = gtk.Window()
    window.connect('delete_event', dummy)

    window.refresh = dummy

    for i in menu_data:
        if i[2]:
            setattr(window, i[2], dummy)

    menu = Menu(window)

    vbox = gtk.VBox()
    window.add(vbox)

    vbox.add(menu.get_widgets()[0])

    window.show_all()

    gtk.main()

if __name__ == '__main__':
    main()
