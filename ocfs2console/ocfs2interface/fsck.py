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

import gobject
import gtk

from guiutil import set_props

base_command = ('fsck.ocfs2',)

try:
    import vte
except ImportError:
    fsck_ok = False
else:
    fsck_ok = True

def fsck_volume(parent, device, check=False):
    if check:
        check_str = 'check'
    else:
        check_str = 'repair'

    title = 'File System ' + check_str.capitalize()

    dialog = gtk.Dialog(parent=parent, title=title,
                        buttons=(gtk.STOCK_CLOSE, gtk.RESPONSE_CLOSE))

    label = gtk.Label(title)
    label.set_alignment(xalign=0.0, yalign=0.5)
    dialog.vbox.pack_start(label)

    frame = gtk.Frame()
    frame.set_shadow_type(gtk.SHADOW_IN)
    dialog.vbox.pack_end(frame)

    hbox = gtk.HBox()
    frame.add(hbox)

    terminal = vte.Terminal()
    terminal.set_scrollback_lines(8192)
    #terminal.set_font_from_string('monospace 12')
    hbox.pack_start(terminal)

    scrollbar = gtk.VScrollbar()
    scrollbar.set_adjustment(terminal.get_adjustment())
    hbox.pack_end(scrollbar)

    dialog.finished = False
    terminal.connect('child-exited', child_exited, dialog)

    dialog.pid = -1
    command = fsck_command(device, check)
    gobject.idle_add(start_command, terminal, command, dialog)

    dialog.show_all()

    while 1:
        dialog.run()

        if dialog.finished:
            break

        msg = ('File system %s is still running. You should not close this '
               'window until it is finished' % check_str)

        info = gtk.MessageDialog(parent=dialog,
                                 flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                                 type=gtk.MESSAGE_WARNING,
                                 buttons=gtk.BUTTONS_CLOSE,
                                 message_format=msg)
        info.run()
        info.destroy()
 
    dialog.destroy()

def start_command(terminal, command, dialog):
    terminal.fork_command(command=command[0], argv=command)
    return False

def child_exited(terminal, dialog):
    dialog.finished = True

def fsck_command(device, check):
    command = list(base_command)

    if check:
        command.append('-n')
    else:
        command.append('-y')

    command.append("'%s'" % device)

    realcommand = '%s; sleep 1' % ' '.join(command)

    return ['/bin/sh', '-c', realcommand]

def main():
    fsck(None, '/dev/sdb1', check=True)

if __name__ == '__main__':
    main()
