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

from guiutil import set_props, error_box, query_text

from process import Process

def mount(parent, device):
    mountpoint = query_text(parent, 'Mountpoint')
    if not mountpoint:
        return None

    command = ('mount', '-t', 'ocfs2', device, mountpoint)

    p = Process(command, 'Mount', 'Mounting...', parent, spin_now=False)
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

    p = Process(command, 'Unmount', 'Unmounting...', parent, spin_now=False)
    success, output, killed = p.reap()

    if not success:
        if killed:
            error_box(parent, 'umount died unexpectedly! Your system is '
                              'probably in an inconsistent state. You '
                              'should reboot at the earliest opportunity')
        else:
            error_box(parent, '%s: Could not unmount %s mounted on %s' %
                              (output, device, mountpoint))
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
