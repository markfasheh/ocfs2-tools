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

import gtk

from guiutil import set_props

try:
    import vte
except ImportError:
    terminal_ok = False
else:
    terminal_ok = True

class TerminalDialog(gtk.Dialog):
    def __init__(self, parent=None, title='Terminal'):
        gtk.Dialog.__init__(self, parent=parent, title=title,
                            buttons=(gtk.STOCK_CLOSE, gtk.RESPONSE_CLOSE))

        label = gtk.Label(title)
        label.set_alignment(xalign=0.0, yalign=0.5)
        self.vbox.pack_start(label)

        frame = gtk.Frame()
        frame.set_shadow_type(gtk.SHADOW_IN)
        self.vbox.pack_end(frame)

        hbox = gtk.HBox()
        frame.add(hbox)

        self.terminal = vte.Terminal()
        self.terminal.set_scrollback_lines(8192)
        #self.terminal.set_font_from_string('monospace 12')
        hbox.pack_start(self.terminal)

        scrollbar = gtk.VScrollbar()
        scrollbar.set_adjustment(self.terminal.get_adjustment())
        hbox.pack_end(scrollbar)

        self.show_all()

def main():
    dialog = TerminalDialog()
    dialog.run()

if __name__ == '__main__':
    main()
