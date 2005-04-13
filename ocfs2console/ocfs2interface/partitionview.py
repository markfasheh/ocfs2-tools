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

from plist import partition_list

COLUMN_DEVICE, COLUMN_MOUNTPOINT = range(2)

class PartitionView(gtk.TreeView):
    def __init__(self, info_frames=()):
        store = gtk.ListStore(str, str, str)

        gtk.TreeView.__init__(self, store)

        self.insert_column_with_attributes(-1, 'Device',
                                           gtk.CellRendererText(),
                                           text=COLUMN_DEVICE)
        self.insert_column_with_attributes(-1, 'Mountpoint',
                                           gtk.CellRendererText(),
                                           text=COLUMN_MOUNTPOINT)

        sel = self.get_selection()
        sel.connect('changed', self.on_select)

        self.filter_entry = None

        self.sel_widgets = []

        self.mount_widgets = []
        self.unmount_widgets = []

        self.info_frames = tuple(info_frames)

    def get_sel_values(self):
        sel = self.get_selection()
        store, iter = sel.get_selected()

        if store and iter:
            return store[iter]
        else:
            return (None, None)

    def get_device(self):
        selection = self.get_sel_values()
        return selection[COLUMN_DEVICE]

    def on_select(self, sel):
        self.selected = True

        self.set_widgets_sensitive(self.sel_widgets, True)

        device, mountpoint = self.get_sel_values()

        if mountpoint:
            self.set_widgets_sensitive(self.mount_widgets, True)
            self.set_widgets_sensitive(self.unmount_widgets, False)
        else:
            self.set_widgets_sensitive(self.mount_widgets, False)
            self.set_widgets_sensitive(self.unmount_widgets, True)

        self.update_info(device)

    def update_info(self, device):
        for frame, info in self.info_frames:
            if frame.child:
                frame.child.destroy()

            frame.add(info(device))
            frame.show_all()

    def select_device(self, device):
        for row in self.get_model():
            if row[COLUMN_DEVICE] == device:
               sel = self.get_selection()
               sel.select_iter(row.iter)

    def refresh_partitions(self):
        def list_compare(store, a, b):
            d1, m1 = store[a]
            d2, m2 = store[b]

            if m1 and not m2:
                return -1
            elif not m1 and m2:
                return 1
            else:
                return cmp(d1, d2)

        self.set_widgets_sensitive(self.sel_widgets, False)
        self.set_widgets_sensitive(self.mount_widgets, False)
        self.set_widgets_sensitive(self.unmount_widgets, False)

        if self.filter_entry:
            filter = self.filter_entry.get_text()
        else:
            filter = None

        old_device = self.get_device()

        store = gtk.ListStore(str, str)
        self.set_model(store)

        self.store = store
        self.sel = self.get_selection()
        self.selected = False

        store.set_sort_func(COLUMN_DEVICE, list_compare)
        store.set_sort_column_id(COLUMN_DEVICE, gtk.SORT_ASCENDING)

        partition_list(self.add_partition, old_device,
                       filter=filter, fstype='ocfs2', async=True)

        if len(store):
            if not self.selected:
                self.sel.select_iter(store.get_iter_first())
        else:
            self.update_info(None)

    def add_partition(self, device, mountpoint, fstype, old_device):
        iter = self.store.append((device, mountpoint))

        if device == old_device:
            self.sel.select_iter(iter)
            self.selected = True

    def set_filter_entry(self, entry):
        self.filter_entry = entry

    def add_to_widget_list(self, widget_list, widgets):
        try:
            widget_list.extend(widgets)
        except TypeError:
            widget_list.append(widgets)

    def add_sel_widgets(self, widgets):
        self.add_to_widget_list(self.sel_widgets, widgets)

    def add_mount_widgets(self, widgets):
        self.add_to_widget_list(self.mount_widgets, widgets)

    def add_unmount_widgets(self, widgets):
        self.add_to_widget_list(self.unmount_widgets, widgets)
            
    def set_widgets_sensitive(self, widgets, sensitive=True):
        for widget in widgets:
            widget.set_sensitive(sensitive)

def main():
    def dummy(*args):
        gtk.main_quit()

    window = gtk.Window()
    window.set_size_request(300, 200)
    window.connect('delete_event', dummy)

    scrl_win = gtk.ScrolledWindow()
    scrl_win.set_policy(gtk.POLICY_AUTOMATIC, gtk.POLICY_AUTOMATIC)
    window.add(scrl_win)

    pv = PartitionView()
    scrl_win.add(pv)

    window.show_all()

    pv.refresh_partitions()

    gtk.main()

if __name__ == '__main__':
    main()
