#!/usr/bin/python

import sys

import gtk

import ocfs2

from guiutil import set_props, error_box, query_text
from process import Process
from format import format_partition
from general import General
from nodemap import NodeMap
from bitmap import Bitmap
from browser import Browser

OCFS2TOOL_VERSION = '0.0.1'

COLUMN_DEVICE = 0
COLUMN_MOUNTPOINT = 1

MODE_BASIC = 0
MODE_ADVANCED = 1

notebook_items = (
    ('general', 'General',          General),
    ('browser', 'File Listing',     Browser),
    ('nodemap', 'Configured Nodes', NodeMap),
    ('bitmap',  'Bitmap View',      Bitmap),
)

def cleanup(*args):
    gtk.main_quit()

def usage(name):
    print '''Usage: %s [OPTION]...
Options:
  -V, --version  print version information and exit
      --help     display this help and exit''' % name

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

def mount(button, pv):
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

def unmount(button, pv):
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

def refresh(button, pv):
    pv.refresh_partitions()

    if len(pv.get_model()) == 0:
        update_notebook(pv, None)

def update_notebook(pv, device):
    for tag, d, info in notebook_items:
        frame = getattr(pv, tag + '-frame')
        frame.child.destroy()

        frame.add(info(device, pv.advanced).widget)
        frame.show_all()

def create_action_area(pv):
    vbbox = gtk.VButtonBox()
    set_props(vbbox, layout_style=gtk.BUTTONBOX_START,
                     spacing=5,
                     border_width=5)
   
    button = gtk.Button("Mount")
    vbbox.add(button)
    button.connect('clicked', mount, pv)
    pv.mount_button = button
   
    button = gtk.Button("Unmount")
    vbbox.add(button)
    button.connect('clicked', unmount, pv)
    pv.unmount_button = button

    button = gtk.Button("Refresh")
    vbbox.add(button)
    button.connect('clicked', refresh, pv)

    return vbbox

def format(pv, action, w):
    format_partition(pv.toplevel, pv.get_device(), pv.advanced)
    pv.refresh_partitions()

def genconfig(pv, action, w):
    pass

def level(pv, action, w):
    if action == MODE_ADVANCED:
        advanced = True
    else:
        advanced = False

    if pv.advanced == advanced:
        return

    pv.advanced = advanced

def about(pv, a, w):
    dialog = gtk.MessageDialog(parent=pv.toplevel,
                               flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                               buttons=gtk.BUTTONS_CLOSE)
    dialog.label.set_text('''OCFS2 Tool
Version %s
Copyright (C) Oracle Corporation 2002, 2004
All Rights Reserved''' % OCFS2TOOL_VERSION)

    dialog.run()
    dialog.destroy()

menu_items = (
    ('/_File',                     None,         None,      0, '<Branch>'),
    ('/File/_Quit',                None,         cleanup,   0, '<StockItem>', gtk.STOCK_QUIT),
    ('/_Tasks',                    None,         None,      0, '<Branch>'),
    ('/Tasks/_Format...',          '<control>F', format,    0),
    #('/Tasks/---',                 None,         None,      0, '<Separator>'),
    #('/Tasks/_Generate Config...', '<control>G', genconfig, 0),
    ('/_Preferences',              None,         None,      0, '<Branch>'),
    ('/Preferences/_Basic',        '<control>B', level,     MODE_BASIC, '<RadioItem>'),
    ('/Preferences/_Advanced',     '<control>A', level,     MODE_ADVANCED, '/Preferences/Basic'),
    ('/_Help',                     None,         None,      0, '<Branch>'),
    ('/Help/_About',               None,         about,     0)
)

def create_window():
    window = gtk.Window()
    set_props(window, title='OCFS2 Tool',
                      default_width=520,
                      default_height=420,
                      border_width=0)
    window.connect('delete_event', cleanup)

    pv = PartitionView(window)

    accel_group = gtk.AccelGroup()
    window.add_accel_group(accel_group)

    item_factory = gtk.ItemFactory(gtk.MenuBar, '<main>', accel_group)
    item_factory.create_items(menu_items, pv)
    window.item_factory = item_factory

    vbox = gtk.VBox()
    window.add(vbox)

    menubar = item_factory.get_widget('<main>')
    vbox.pack_start(menubar, expand=False, fill=False)

    vpaned = gtk.VPaned()
    vpaned.set_border_width(4)
    vbox.pack_start(vpaned, expand=True, fill=True)

    hbox = gtk.HBox()
    vpaned.pack1(hbox)

    scrl_win = gtk.ScrolledWindow()
    set_props(scrl_win, hscrollbar_policy=gtk.POLICY_AUTOMATIC,
                        vscrollbar_policy=gtk.POLICY_AUTOMATIC,
                        parent=hbox)

    scrl_win.add(pv)

    frame = gtk.Frame()
    frame.set_shadow_type(gtk.SHADOW_IN)
    hbox.pack_end(frame, expand=False, fill=False)

    vbbox = create_action_area(pv)
    frame.add(vbbox)

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
    for arg in sys.argv[1:]:
        if arg == '--version' or arg == '-V':
            print 'OCFS2Tool version %s' % OCFS2TOOL_VERSION
            sys.exit(0)
        elif arg == '--help':
            usage(sys.argv[0])
            sys.exit(0)
        else:
            usage(sys.argv[0])
            sys.exit(1)

    create_window()

    gtk.main()

if __name__ == '__main__':
    main()
