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
import pango

import ocfs2

from guiutil import set_props

fields = ('Perms', '# Links', 'Owner', 'Group', 'Size', 'Alloc Size',
          'Timestamp', 'Name')
sample = ('-rw-r--r--', '1', 'manish', 'manish', '133194', '262144', 'Sep 29 12:46', 'closobo.c')

class Browser:
     def __init__(self, device=None, advanced=False):
         self.widget = gtk.VBox(spacing=4)

         scrl_win = gtk.ScrolledWindow()
         scrl_win.set_policy(gtk.POLICY_AUTOMATIC, gtk.POLICY_AUTOMATIC)
         self.widget.add(scrl_win)

         tv = gtk.TreeView()
         scrl_win.add(tv)

         tv.insert_column_with_attributes(-1, 'File', gtk.CellRendererText(),
                                          text=0)

         table = gtk.Table(rows=2, columns=7)
         set_props(table, row_spacing=4,
                          column_spacing=4,
                          border_width=4)
         self.widget.pack_end(table, expand=False, fill=False)

         font = pango.FontDescription('Monospace')

         for i in range(0, len(fields)):
             label = gtk.Label(fields[i])
             set_props(label, xalign=0.0)
             table.attach(label, i, i + 1, 0, 1)

             label = gtk.Label(sample[i])
             if i == 1 or i == 4 or i == 5:
                 set_props(label, xalign=1.0)
             else:
                 set_props(label, xalign=0.0)
             table.attach(label, i, i + 1, 1, 2)

             label.modify_font(font)
