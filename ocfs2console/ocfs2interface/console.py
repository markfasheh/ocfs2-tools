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

from guiutil import set_props, error_box, query_text

from menu import Menu
from toolbar import Toolbar
from about import about, process_gui_args
from process import Process
from format import format_partition
from tune import tune_label, tune_nodes
from general import General
from nodemap import NodeMap
from browser import Browser
from clconfig import cluster_configurator
from fsck import fsck_volume

COLUMN_DEVICE = 0
COLUMN_MOUNTPOINT = 1

notebook_items = (
    ('general', 'General',          General),
    ('browser', 'File Listing',     Browser),
    ('nodemap', 'Configured Nodes', NodeMap),
)

class PartitionView(gtk.TreeView):
    def __init__(self):
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

        device, mountpoint = self.get_sel_values()

        if mountpoint:
            self.mount_button.set_sensitive(False)
            self.unmount_button.set_sensitive(True)
        else:
            self.mount_button.set_sensitive(True)
            self.unmount_button.set_sensitive(False)

        self.update_notebook(device)

    def update_notebook(self, device):
        for tag, d, info in notebook_items:
            frame = getattr(self, tag + '_frame')
            frame.child.destroy()

            frame.add(info(device).widget)
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

        self.mount_button.set_sensitive(False)
        self.unmount_button.set_sensitive(False)

        filter = self.filter_entry.get_text()

        old_device = self.get_device()

        store = gtk.ListStore(str, str)
        self.set_model(store)

        self.store = store
        self.sel = self.get_selection()
        self.selected = False

        store.set_sort_func(COLUMN_DEVICE, list_compare)
        store.set_sort_column_id(COLUMN_DEVICE, gtk.SORT_ASCENDING)

        ocfs2.partition_list(self.add_partition, data=old_device,
                             filter=filter, fstype='ocfs2', async=True)

        if len(store) and not self.selected:
            self.sel.select_iter(store.get_iter_first())

    def add_partition(self, device, mountpoint, fstype, old_device):
        iter = self.store.append((device, mountpoint))

        if device == old_device:
            self.sel.select_iter(iter)
            self.selected = True

class Console(gtk.Window):
    def __init__(self):
        gtk.Window.__init__(self)

        set_props(self, title='OCFS2 Console',
                        default_width=520,
                        default_height=420,
                        border_width=0)
        self.connect('delete_event', self.cleanup)

        pv = PartitionView()

        vbox = gtk.VBox()
        self.add(vbox)

        self.menu = Menu(self)

        menubar = self.menu.get_widget(pv)
        vbox.pack_start(menubar, expand=False, fill=False)

        self.toolbar = Toolbar(self)

        tb, buttons, pv.filter_entry = self.toolbar.get_widgets(pv)
        vbox.pack_start(tb, expand=False, fill=False)

        for k, v in buttons.iteritems():
            setattr(pv, k + '_button', v)

        pv.filter_entry.connect('activate', self.filter_update, pv)

        vpaned = gtk.VPaned()
        vpaned.set_border_width(4)
        vbox.pack_start(vpaned, expand=True, fill=True)

        scrl_win = gtk.ScrolledWindow()
        set_props(scrl_win, hscrollbar_policy=gtk.POLICY_AUTOMATIC,
                            vscrollbar_policy=gtk.POLICY_AUTOMATIC)
        scrl_win.add(pv)
        vpaned.pack1(scrl_win)

        notebook = gtk.Notebook()
        notebook.set_tab_pos(gtk.POS_TOP)
        vpaned.pack2(notebook)

        for tag, desc, info in notebook_items:
            frame = gtk.Frame()
            set_props(frame, shadow=gtk.SHADOW_NONE,
                             border_width=0)

            tag = tag + '_frame'
            setattr(pv, tag, frame)

            frame.add(info().widget)
            frame.show_all()

            notebook.add_with_properties(frame, 'tab_label', desc)

        pv.refresh_partitions()
        pv.grab_focus()

        self.show_all()

    def cleanup(self, *args):
        gtk.main_quit()

    def about(self, pv):
        about(self)

    def mount(self, pv):
        device, mountpoint = pv.get_sel_values()

        mountpoint = query_text(self, 'Mountpoint')
        if not mountpoint:
            return

        command = ('mount', '-t', 'ocfs2', device, mountpoint)

        p = Process(command, 'Mount', 'Mounting...', self, spin_now=False)
        success, output, killed = p.reap()

        if not success:
            if killed:
                error_box(self, 'mount died unexpectedly! Your system is '
                                'probably in an inconsistent state. You '
                                'should reboot at the earliest opportunity')
            else:
                error_box(self, '%s: Could not mount %s' % (output, device))

        pv.refresh_partitions()

    def unmount(pv):
        device, mountpoint = pv.get_sel_values()

        command = ('umount', mountpoint)

        p = Process(command, 'Unmount', 'Unmounting...', self, spin_now=False)
        success, output, killed = p.reap()

        if success:
            pv.refresh_partitions()
        else:
            if killed:
                error_box(self, 'umount died unexpectedly! Your system is '
                                'probably in an inconsistent state. You '
                                'should reboot at the earliest opportunity')
            else:
                error_box(self, '%s: Could not unmount %s mounted on %s' %
                                (output, device, mountpoint))

    def refresh(self, pv):
        pv.refresh_partitions()

        if len(pv.get_model()) == 0:
            pv.update_notebook(None)

    def format(self, pv):
        format_partition(self, pv.get_device())
        pv.refresh_partitions()

    def relabel(self, pv):
        tune_label(self, pv.get_device())
        pv.refresh_partitions()

    def node_num(self, pv):
        tune_nodes(self, pv.get_device())
        pv.refresh_partitions()

    def check(self, pv):
        fsck_volume(self, pv.get_device(), check=True)
        pv.refresh_partitions()

    def repair(self, pv):
        fsck_volume(self, pv.get_device(), check=False)
        pv.refresh_partitions()

    def clconfig(self, pv):
        cluster_configurator(self)

    def filter_update(self, entry, pv):
        refresh(pv)

def main():
    process_gui_args()
    console = Console()
    gtk.main()

if __name__ == '__main__':
    main()
