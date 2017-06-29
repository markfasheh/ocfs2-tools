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
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.

import gtk
import gobject

from terminal import TerminalDialog, terminal_ok as fsck_ok

base_command = ('fsck.ocfs2',)

def fsck_volume(parent, device, check=False):
    if check:
        check_str = 'check'
    else:
        check_str = 'repair'

    title = 'File System ' + check_str.capitalize()

    dialog = TerminalDialog(parent=parent, title=title)
    terminal = dialog.terminal

    dialog.finished = False
    terminal.connect('child-exited', child_exited, dialog)

    command = fsck_command(device, check)
    gobject.idle_add(start_command, terminal, command, dialog)

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
    import sys
    fsck_volume(None, sys.argv[1], check=True)

if __name__ == '__main__':
    main()
