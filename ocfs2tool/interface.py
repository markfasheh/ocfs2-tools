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

from guiutil import set_props, error_box, query_text

from menu import Menu
from toolbar import Toolbar
from about import about
from process import Process
from format import format_partition
from general import General
from nodemap import NodeMap
from browser import Browser
from clconfig import cluster_configurator

COLUMN_DEVICE = 0
COLUMN_MOUNTPOINT = 1

notebook_items = (
    ('general', 'General',          General),
    ('browser', 'File Listing',     Browser),
    ('nodemap', 'Configured Nodes', NodeMap),
)

def cleanup(*args):
    gtk.main_quit()

class PartitionView(gtk.TreeView):
    def __init__(self, toplevel):
        store = gtk.ListStore(str, str, str)

        gtk.TreeView.__init__(self, store)

        self.toplevel = toplevel
        self.advanced = False

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
            return None

    def get_device(self):
        selection = self.get_sel_values()

        if selection:
            return selection[COLUMN_DEVICE]
        else:
            return None

    def on_select(self, sel):
        device, mountpoint = self.get_sel_values()

        if mountpoint:
            self.mount_button.set_sensitive(False)
            self.unmount_button.set_sensitive(True)
        else:
            self.mount_button.set_sensitive(True)
            self.unmount_button.set_sensitive(False)

        update_notebook(self, device)

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

        old_device = self.get_device()

        store = gtk.ListStore(str, str)
        self.set_model(store)

        sel = self.get_selection()
        selected = False

        for partition in ocfs2.partition_list():
            iter = store.append(partition)

            if partition[0] == old_device:
                sel.select_iter(iter)
                selected = True

        store.set_sort_func(COLUMN_DEVICE, list_compare)
        store.set_sort_column_id(COLUMN_DEVICE, gtk.SORT_ASCENDING)

        if len(store) and not selected:
            sel.select_iter(store.get_iter_first())

def mount(pv):
    device, mountpoint = pv.get_sel_values()

    mountpoint = query_text(pv.toplevel, 'Mountpoint')
    if not mountpoint:
        return

    command = ('mount', '-t', 'ocfs2', device, mountpoint)

    p = Process(command, 'Mount', 'Mounting...', pv.toplevel, spin_now=False)
    success, output, killed = p.reap()

    if not success:
        if killed:
            error_box(pv.toplevel,
                      'mount died unexpectedly! Your system is probably in '
                      'an inconsistent state. You should reboot at the '
                      'earliest opportunity')
        else:
            error_box(pv.toplevel, '%s: Could not mount %s' % (output, device))

    pv.refresh_partitions()

def unmount(pv):
    device, mountpoint = pv.get_sel_values()

    command = ('umount', mountpoint)

    p = Process(command, 'Unmount', 'Unmounting...', pv.toplevel,
                spin_now=False)
    success, output, killed = p.reap()

    if success:
        pv.refresh_partitions()
    else:
        if killed:
            error_box(pv.toplevel,
                      'umount died unexpectedly! Your system is probably in '
                      'an inconsistent state. You should reboot at the '
                      'earliest opportunity')
        else:
            error_box(pv.toplevel,
                      '%s: Could not unmount %s mounted on %s' %
                      (output, device, mountpoint))

def refresh(pv):
    pv.refresh_partitions()

    if len(pv.get_model()) == 0:
        update_notebook(pv, None)

def update_notebook(pv, device):
    for tag, d, info in notebook_items:
        frame = getattr(pv, tag + '-frame')
        frame.child.destroy()

        frame.add(info(device, pv.advanced).widget)
        frame.show_all()

def format(pv):
    format_partition(pv.toplevel, pv.get_device(), pv.advanced)
    pv.refresh_partitions()

def check(pv):
    pass

def repair(pv):
    pass

def clconfig(pv):
    cluster_configurator(pv.toplevel, pv.advanced)

def create_window():
    window = gtk.Window()
    set_props(window, title='OCFS2 Tool',
                      default_width=520,
                      default_height=420,
                      border_width=0)
    window.connect('delete_event', cleanup)

    pv = PartitionView(window)

    vbox = gtk.VBox()
    window.add(vbox)

    menu = Menu(cleanup=cleanup, format=format, check=check, repair=repair,
                clconfig=clconfig, about=about)

    menubar = menu.get_widget(window, pv)
    vbox.pack_start(menubar, expand=False, fill=False)

    toolbar = Toolbar(mount=mount, unmount=unmount, refresh=refresh)

    tb, buttons = toolbar.get_widgets(pv)
    vbox.pack_start(tb, expand=False, fill=False)

    for k, v in buttons.iteritems():
        setattr(pv, k + '_button', v)
 
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

        tag = tag + '-frame'
        setattr(pv, tag, frame)

        frame.add(info().widget)
        frame.show_all()

        notebook.add_with_properties(frame, 'tab_label', desc)

    pv.refresh_partitions()

    window.show_all()

def main():
    create_window()
    gtk.main()

if __name__ == '__main__':
    main()
