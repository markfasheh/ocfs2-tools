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

import ocfs2

from guiutil import set_props, error_box, format_bytes
from process import Process

from fswidgets import NumNodes, VolumeLabel

base_command = ('tunefs.ocfs2', '-x')

class TuneVolumeLabel(VolumeLabel):
    def __init__(self, device=None):
        VolumeLabel.__init__(self)

        if device:
            try:
                super = ocfs2.get_super(device)
                self.set_text(super.s_label)
            except ocfs2.error:
                pass

    title = 'Changing Label'
    action = 'Changing label'

    empty_ok = True

class TuneNumNodes(NumNodes):
    def __init__(self, device=None):
        NumNodes.__init__(self)

        if device:
            super = ocfs2.get_super(device)
            self.set_range(super.s_max_nodes, ocfs2.MAX_NODES)

    title = 'Edit Node Count'
    action = 'Changing node count'

    empty_ok = False

def tune_action(widget_type, parent, device):
    try:
        widget = widget_type(device)
    except ocfs2.error:
        error_box(parent, 'Could not get current %s for device $s' %
                          (widget_type.label.lower(), device))
        return False

    dialog = gtk.Dialog(parent=parent, title=widget_type.title,
                        buttons=(gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL,
                                 gtk.STOCK_OK,     gtk.RESPONSE_OK))

    table = gtk.Table(rows=1, columns=2)
    set_props(table, row_spacing=4,
                     column_spacing=4,
                     border_width=4,
                     parent=dialog.vbox)

    label = gtk.Label(widget_type.label + ':')
    set_props(label, xalign=1.0)
    table.attach(label, 0, 1, 0, 1)

    table.attach(widget, 1, 2, 0, 1)

    widget.grab_focus()

    dialog.show_all()

    while 1:
        if dialog.run() != gtk.RESPONSE_OK:
            dialog.destroy()
            return False

        new_label = widget.get_text()

        if not new_label:
            if widget_type.empty_ok:
                msg = ('Are you sure you want to clear the %s on %s?' %
                       (widget_type.lower(), device))

                ask = gtk.MessageDialog(parent=dialog,
                                        flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                                        type=gtk.MESSAGE_QUESTION,
                                        buttons=gtk.BUTTONS_YES_NO,
                                        message_format=msg)
                        
                if ask.run() == gtk.RESPONSE_YES:
                    break
                else:
                    ask.destroy()
            else:
                error_box(dialog, '%s cannot be empty.' %
                                  widget_type.lower().ucfirst())
        else:
            break

    command = list(base_command)
    command.extend(widget.get_arg())

    dialog.destroy()

    tunefs = Process(command, widget_type.title, widget_type.action + '...',
                     parent)
    success, output, k = tunefs.reap()

    if not success:
        error_box(parent, 'File system tune error: %s' % output)
        return False

    return True

def tune_label(parent, device):
    tune_action(TuneVolumeLabel, parent, device)

def tune_nodes(parent, device):
    tune_action(TuneNumNodes, parent, device)

def main():
    tune_label(None, None)
    tune_nodes(None, None)

if __name__ == '__main__':
    main()
