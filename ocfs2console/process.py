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

import os
import types
import fcntl
import popen2

import gobject
import gtk

from guiutil import set_props

INTERVAL = 100
TIMEOUT = 10000

class Process:
    def __init__(self, command, title, desc, parent=None, spin_now=True):
        if isinstance(command, types.StringTypes):
            if len(command.split(None, 1)) < 2:
                command = (command,)

        self.command = command

        self.title = title
        self.desc = desc

        self.parent = parent

        self.spin_now = spin_now

        self.pipe = popen2.Popen4(self.command)

    def reap(self):
        self.success = False
        self.killed = False

        self.count = TIMEOUT // INTERVAL
        self.threshold = self.count - INTERVAL * 10

        self.dialog = None

        if self.spin_now:
            self.count = TIMEOUT * 60
            self.make_progress_box()

        timeout_id = gobject.timeout_add(INTERVAL, self.timeout)

        fromchild = self.pipe.fromchild

        fileno = fromchild.fileno()
        flags = fcntl.fcntl(fileno, fcntl.F_GETFL, 0)
        flags = flags | os.O_NONBLOCK
        fcntl.fcntl(fileno, fcntl.F_SETFL, flags)

        self.output = ''
        output_id = gobject.io_add_watch(fromchild, gobject.IO_IN, self.read)

        gtk.main()

        if self.dialog:
            self.dialog.destroy()

        gobject.source_remove(output_id)
        gobject.source_remove(timeout_id)

        if not self.success:
            if self.killed:
                if self.output:
                    self.output += '\n'

                self.output += 'Killed prematurely.'

        return self.success, self.output, self.killed

    def timeout(self):
        self.count = self.count - 1

        ret = self.pipe.poll()

        if ret != -1:
            self.success = not os.WEXITSTATUS(ret)
            gtk.main_quit()
            return True

        if self.count < 1:
            self.kill()
            return True

        if self.count < self.threshold and not self.dialog:
            self.make_progess_box()

        if self.dialog:
            self.pbar.pulse()

        return True

    def kill(self):
        self.success = False
        self.killed = True

        os.kill(self.pipe.pid, 15)
        gobject.timeout_add(INTERVAL * 5, self.kill_timeout)

        gtk.main_quit()

    def kill_timeout(self):
        if self.pipe.poll() == -1:
            os.kill(self.pipe.pid, 9)
            self.kill_9 = True

        return False

    def make_progress_box(self):
        self.dialog = gtk.Window()
        set_props(self.dialog, title=self.title,
                               resizable=False,
                               modal=True,
                               type_hint=gtk.gdk.WINDOW_TYPE_HINT_DIALOG)

        def ignore(w, e):
            return True
        self.dialog.connect('delete-event', ignore)

        self.dialog.set_transient_for(self.parent)

        vbox = gtk.VBox()
        set_props(vbox, spacing=0,
                        homogeneous=False,
                        border_width=4,
                        parent=self.dialog)

        label = gtk.Label(self.desc + '...')
        vbox.pack_start(label, expand=False, fill=False)

        self.pbar = gtk.ProgressBar()
        vbox.pack_start(self.pbar, expand=False, fill=False)

        self.dialog.show_all()

    def read(self, fd, cond):
        if cond & gtk.gdk.INPUT_READ:
            try:
                self.output += fd.read(1024)
            except IOError, e:
                return False

        return True

def main():
    process = Process('echo Hello; sleep 10', 'Sleep', 'Sleeping')
    print process.reap()

if __name__ == '__main__':
    main() 
