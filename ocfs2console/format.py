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

import ocfs2

from guiutil import set_props, error_box, format_bytes
from process import Process

if hasattr(gtk, 'ComboBox'):
    class BaseCombo(gtk.ComboBox):
        def __init__(self):
            self.store = gtk.ListStore(str)
            gtk.ComboBox.__init__(self, model=self.store)

            cell = gtk.CellRendererText()
            self.pack_start(cell)
            self.set_attributes(cell, text=0)

        def get_choice(self):
            return self.store[self.get_active_iter()][0]

        def set_choices(self, choices):
            selected = False

            for choice, select in choices:
                iter = self.store.append((choice,))

                if select:
                    self.set_active_iter(iter)
                    selected = True

            if not selected:
                self.set_active(0)

else:
    class BaseCombo(gtk.Combo):
        def __init__(self):
            gtk.Combo.__init__(self)
            self.entry.set_editable(False)

        def get_choice(self):
            return self.entry.get_text()

        def set_choices(self, choices):
            for choice, select in choices:
                item = gtk.ListItem(choice)
                item.show()
                self.list.add(item)

                if select:
                    item.select()

class ValueCombo(BaseCombo):
    def __init__(self, minimum, maximum):
        BaseCombo.__init__(self)

        choices = [('Auto', False)]

        size = minimum
        while size <= maximum:
            choices.append((format_bytes(size), False))
            size = size << 1

        self.set_choices(choices)

    def get_arg(self):
        text = self.get_choice()

        if text != 'Auto':
            s = text.replace(' ', '')
            s = s.replace('B', '')
            s = s.replace('bytes', '')
            return (self.arg, s)
        else:
            return None

class NumNodes(gtk.SpinButton):
    def __init__(self):
        adjustment = gtk.Adjustment(4, 2, ocfs2.MAX_NODES, 1, 10)
        gtk.SpinButton.__init__(self, adjustment=adjustment)

    def get_arg(self):
        s = self.get_text()

        if s:
            return ('-n', s)
        else:
            return None

class Device(BaseCombo):
    def fill(self, partitions, device):
        self.set_choices([(p, p == device) for p in partitions])

    def get_device(self):
        return self.get_choice()

class VolumeLabel(gtk.Entry):
    def __init__(self):
        gtk.Entry.__init__(self, max=ocfs2.MAX_VOL_LABEL_LEN)
        self.set_text('oracle')

    def get_arg(self):
        s = self.get_text()

        if s:
            return ('-L', s)
        else:
            return None

class ClusterSize(ValueCombo):
    def __init__(self):
        ValueCombo.__init__(self, ocfs2.MIN_CLUSTER_SIZE,
                                  ocfs2.MAX_CLUSTER_SIZE)
        self.arg = '-c'

class BlockSize(ValueCombo):
    def __init__(self):
        ValueCombo.__init__(self, ocfs2.MIN_BLOCKSIZE,
                                  ocfs2.MAX_BLOCKSIZE)
        self.arg = '-b'

entries = (
    ('Device', Device),
    ('Volume Label', VolumeLabel),
    ('Cluster Size', ClusterSize),
    ('Number of Nodes', NumNodes),
    ('Block Size', BlockSize)
)

def format_partition(parent, device):
    partitions = ocfs2.partition_list(unmounted=True)

    if not partitions:
        error_box(parent, 'No unmounted partitions')
        return False

    dialog = gtk.Dialog(parent=parent, title='Format',
                        buttons=(gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL,
                                 gtk.STOCK_OK,     gtk.RESPONSE_OK))

    table = gtk.Table(rows=5, columns=2)
    set_props(table, row_spacing=4,
                     column_spacing=4,
                     border_width=4,
                     parent=dialog.vbox)

    partitions.sort()

    widgets = []
    row = 0

    for desc, widget_type in entries:
        label = gtk.Label(desc + ':')
        set_props(label, xalign=1.0)
        table.attach(label, 0, 1, row, row + 1)

        widget = widget_type()
        table.attach(widget, 1, 2, row, row + 1)

        if widget_type == Device:
            widget.fill(partitions, device)

        widgets.append(widget)

        row = row + 1

    widgets[0].grab_focus()

    dialog.show_all()

    if dialog.run() != gtk.RESPONSE_OK:
        dialog.destroy()
        return False

    dev = widgets[0].get_device()
    msg = 'Are you sure you want to format %s?' % dev

    ask = gtk.MessageDialog(parent=dialog,
                            flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                            type=gtk.MESSAGE_QUESTION,
                            buttons=gtk.BUTTONS_YES_NO,
                            message_format=msg)

    if ask.run() != gtk.RESPONSE_YES:
        dialog.destroy()
        return False

    command = ['mkfs.ocfs2']
    for widget in widgets[1:]:
        arg = widget.get_arg()

        if arg:
            command.extend(arg)

    command.append(dev)

    dialog.destroy()

    mkfs = Process(command, 'Format', 'Formatting...', parent)
    success, output, k = mkfs.reap()

    if not success:
        error_box(parent, 'Format error: %s' % output)
        return False

    return True

def main():
    format_partition(None, None)

if __name__ == '__main__':
    main()
