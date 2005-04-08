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
import pango

import ocfs2

from guiutil import set_props

class Browser:
     def __init__(self, device=None):
         self.widget = gtk.VBox(spacing=4)

         scrl_win = gtk.ScrolledWindow()
         scrl_win.set_policy(gtk.POLICY_AUTOMATIC, gtk.POLICY_AUTOMATIC)
         self.widget.add(scrl_win)

         tv = gtk.TreeView()
         scrl_win.add(tv)

         tv.insert_column_with_attributes(-1, 'File', gtk.CellRendererText(),
                                          text=0)

def main():
    import sys

    def dummy(*args):
        gtk.main_quit()

    window = gtk.Window()
    window.connect('delete_event', dummy)

    browser = Browser(sys.argv[1]).widget
    window.add(browser)

    window.show_all()

    gtk.main()

if __name__ == '__main__':
    main()
