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

from guiutil import set_props, error_box

def cluster_configurator(parent):
    dialog = gtk.Dialog(parent=parent, title='Cluster Configurator',
                        buttons=(gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL,
                                 gtk.STOCK_OK,     gtk.RESPONSE_OK))

    hbox = gtk.HBox(spacing=4)
    hbox.set_border_width(4)
    dialog.vbox.add(hbox)

    tv = gtk.TreeView()

    scrl_win = gtk.ScrolledWindow()     
    set_props(scrl_win, hscrollbar_policy=gtk.POLICY_AUTOMATIC,
                        vscrollbar_policy=gtk.POLICY_AUTOMATIC,
                        parent=hbox)

    scrl_win.add(tv)

    frame = gtk.Frame()
    frame.set_shadow_type(gtk.SHADOW_IN)
    hbox.pack_end(frame, expand=False, fill=False)

    vbbox = gtk.VButtonBox()
    set_props(vbbox, layout_style=gtk.BUTTONBOX_START,
                     spacing=5,
                     border_width=5,
                     parent=frame)

    button = gtk.Button('Add')
    vbbox.add(button)

    dialog.show_all()

    if dialog.run() != gtk.RESPONSE_OK:
        dialog.destroy()
        return False

    dialog.destroy()
    return True

def main():
    cluster_configurator(None, True)

if __name__ == '__main__':
    main()
