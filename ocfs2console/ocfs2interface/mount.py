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
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.

import gtk

import ocfs2

from guiutil import set_props, error_box

from fstab import FSTab
from process import Process

def mount(parent, device):
    mountpoint, options = query_mount(parent, device)

    if not mountpoint:
        return None

    command = ('mount', '-t', 'ocfs2', device, mountpoint)

    if options:
        command = list(command)
        command[1:1] = ('-o', options)

    p = Process(command, 'Mount', 'Mounting...', parent, spin_now=True)
    success, output, killed = p.reap()

    if not success:
        if killed:
            error_box(parent, 'mount died unexpectedly! Your system is '
                              'probably in an inconsistent state. You '
                              'should reboot at the earliest opportunity')
        else:
            error_box(parent, '%s: Could not mount %s' % (output, device))

        return None
    else:
        return mountpoint

def unmount(parent, device, mountpoint):
    command = ('umount', mountpoint)

    p = Process(command, 'Unmount', 'Unmounting...', parent)
    success, output, killed = p.reap()

    if not success:
        if killed:
            error_box(parent, 'umount died unexpectedly! Your system is '
                              'probably in an inconsistent state. You '
                              'should reboot at the earliest opportunity')
        else:
            error_box(parent, '%s: Could not unmount %s mounted on %s' %
                              (output, device, mountpoint))

def query_mount(parent, device):
    default_mountpoint, default_options = get_defaults(device)

    dialog = gtk.Dialog(parent=parent,
                        flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                        buttons=(gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL,
                                 gtk.STOCK_OK,     gtk.RESPONSE_OK))

    table = gtk.Table(rows=2, columns=2)
    set_props(table, row_spacing=6,
                     column_spacing=6,
                     border_width=6,
                     parent=dialog.vbox)

    def text_changed(entry):
        text = entry.get_text()
        valid = len(text) > 1 and text.startswith('/')
        dialog.set_response_sensitive(gtk.RESPONSE_OK, valid)

    mountpoint = gtk.Entry()
    mountpoint.connect('changed', text_changed)

    mountpoint.set_text(default_mountpoint)
    text_changed(mountpoint)

    options = gtk.Entry()
    options.set_text(default_options)

    row = 0
    for prompt, entry in (('_Mountpoint', mountpoint),
                          ('O_ptions',    options)):
        label = gtk.Label()
        label.set_text_with_mnemonic(prompt + ':')
        set_props(label, xalign=0.0)
        table.attach(label, 0, 1, row, row + 1)

        entry.set_activates_default(True)
        label.set_mnemonic_widget(entry)
        table.attach(entry, 1, 2, row, row + 1)

        row = row + 1

    dialog.show_all()

    if dialog.run() == gtk.RESPONSE_OK:
        mount_params = mountpoint.get_text(), options.get_text()
    else:
        mount_params = None, None

    dialog.destroy()

    return mount_params

def get_defaults(device):
    label, uuid = get_ocfs2_id(device)

    fstab = FSTab()
    entry = fstab.get(device=device, label=label, uuid=uuid)

    if entry and entry.vfstype == 'ocfs2':
        return entry.mountpoint, entry.options
    else:
        return '', ''

def get_ocfs2_id(device):
    try:
        fs = ocfs2.Filesystem(device)
        super = fs.fs_super

        label = super.s_label
        uuid = super.uuid_unparsed
    except ocfs2.error:
        label = uuid = None

    return (label, uuid)

def main():
    import sys

    device = sys.argv[1]

    def dummy(*args):
        gtk.main_quit()

    window = gtk.Window()
    window.connect('delete-event', dummy)

    vbbox = gtk.VButtonBox()
    window.add(vbbox)

    window.mountpoint = None

    def test_mount(b):
        window.mountpoint = mount(window, device)

    button = gtk.Button('Mount')
    button.connect('clicked', test_mount)
    vbbox.add(button)

    def test_unmount(b):
        unmount(window, device, window.mountpoint)

    button = gtk.Button('Unmount')
    button.connect('clicked', test_unmount)
    vbbox.add(button)

    window.show_all()

    gtk.main()

if __name__ == '__main__':
    main()
