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

from cStringIO import StringIO

from guiutil import set_props, error_box

from process import Process

COLUMN_NAME, COLUMN_NODE, COLUMN_IP_ADDR, COLUMN_IP_PORT = range(4)

fields = (
    (COLUMN_NAME,    'Name',       gtk.Entry),
    (COLUMN_NODE,    'Node',       None),
    (COLUMN_IP_ADDR, 'IP Address', gtk.Entry),
    (COLUMN_IP_PORT, 'IP Port',    gtk.SpinButton)
)

class ConfError(Exception):
    pass

class ClusterConf(gtk.HBox):
    def __init__(self, toplevel=None):
        self.toplevel = toplevel

        self.get_cluster_state()

        gtk.HBox.__init__(self, spacing=4)
        self.set_border_width(4)

        self.tv = gtk.TreeView(self.store)
        self.tv.set_size_request(350, 200)

        for col, title, widget_type in fields:
            self.tv.insert_column_with_attributes(-1, title,
                                                  gtk.CellRendererText(),
                                                  text=col)

        scrl_win = gtk.ScrolledWindow()     
        scrl_win.set_policy(hscrollbar_policy=gtk.POLICY_AUTOMATIC,
                            vscrollbar_policy=gtk.POLICY_AUTOMATIC)
        self.pack_start(scrl_win)

        scrl_win.add(self.tv)

        vbbox = gtk.VButtonBox()
        set_props(vbbox, layout_style=gtk.BUTTONBOX_START,
                         spacing=5,
                         border_width=5)
        self.pack_end(vbbox, expand=False, fill=False)

        button = gtk.Button(stock=gtk.STOCK_ADD)
        button.connect('clicked', self.add_node)
        vbbox.add(button)

#        button = gtk.Button(stock=gtk.STOCK_APPLY)
#        button.connect('clicked', self.apply_changes)
#        vbbox.add(button)

    def get_cluster_state(self):
        command = ('o2cb_ctl', '-I', '-t', 'node', '-o')

        o2cb_ctl = Process(command, 'Cluster Control', 'Querying nodes...',
                           self.toplevel, spin_now=False)
        success, output, k = o2cb_ctl.reap()

        if not success:
            raise ConfError, output

        self.store = gtk.ListStore(str, str, str, str)
        self.store.set_sort_column_id(COLUMN_NODE, gtk.SORT_ASCENDING)

        buffer = StringIO(output)

        for line in buffer:
            if line.startswith('#'):
                continue

            try:
                name, cluster, node, ip_addr, ip_port, state = line.split(':')
            except ValueError:
                continue

            if cluster == 'ocfs2':
                iter = self.store.append((name, node, ip_addr, ip_port))

    def add_node(self, b):
        toplevel = self.get_toplevel()

        dialog = gtk.Dialog(parent=toplevel, title='Add Node',
                            buttons=(gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL,
                                     gtk.STOCK_OK,     gtk.RESPONSE_OK))

        table = gtk.Table(rows=4, columns=2)
        set_props(table, row_spacing=4,
                         column_spacing=4,
                         border_width=4,
                         parent=dialog.vbox)

        widgets = []
        row = 0

        for col, title, widget_type in fields:
            if widget_type is None:
                widgets.append(None)
                continue

            label = gtk.Label(title + ':')
            set_props(label, xalign=1.0)
            table.attach(label, 0, 1, row, row + 1)

            widget = widget_type()
            table.attach(widget, 1, 2, row, row + 1)

            if isinstance(widget, gtk.SpinButton):
                widget.set_numeric(True)

                adjustment = gtk.Adjustment(7777, 1000, 30000, 1, 100) 
                widget.set_adjustment(adjustment)

                widget.set_value(7777)
        
            widgets.append(widget)

            row = row + 1

        dialog.show_all()

        while 1:
            if dialog.run() != gtk.RESPONSE_OK:
                dialog.destroy()
                return

            name = widgets[COLUMN_NAME].get_text()
            ip_addr = widgets[COLUMN_IP_ADDR].get_text()
            ip_port = widgets[COLUMN_IP_PORT].get_text()

            if not name:
                error_box(dialog, 'Node name not specified')
            elif not ip_addr:
                error_box(dialog, 'IP address not specified')
            else:
                break

        dialog.destroy()

        command = ('o2cb_ctl', '-I', '-t', 'cluster', '-n', 'ocfs2', '-o')
        o2cb_ctl = Process(command, 'Cluster Control', 'Adding node...',
                           self.toplevel, spin_now=False)
        success, output, k = o2cb_ctl.reap()

        if not success:
            command = ('o2cb_ctl', '-C', '-n', 'ocfs2', '-t', 'cluster', '-i')
            success, output, k = o2cb_ctl.reap()

            if not success:
                error_box(self.toplevel,
                          '%s\nCould not create cluster' % output)
                return

            
        command = ('o2cb_ctl', '-C', '-n', name, '-t', 'node',
                   '-a', 'cluster=ocfs2',
                   '-a', 'ip_address=%s' % ip_addr,
                   '-a', 'ip_port=%s' % ip_port,
                   '-i')

        o2cb_ctl = Process(command, 'Cluster Control', 'Adding node...',
                           self.toplevel, spin_now=False)
        success, output, k = o2cb_ctl.reap()

        if not success:
            error_box(self.toplevel,
                      '%s\nCould not update configuration' % output)
            return

        self.get_cluster_state()
        self.tv.set_model(self.store)

    def apply_changes(self, b):
        pass

def cluster_configurator(parent):
    try:
        conf = ClusterConf(parent)
    except ConfError, e:
        error_box(parent, '%s: Could not query cluster configuration' % str(e))
        return

    dialog = gtk.Dialog(parent=parent, title='Cluster Configurator',
                        buttons=(gtk.STOCK_CLOSE, gtk.RESPONSE_CLOSE))

    dialog.vbox.add(conf)
    dialog.show_all()

    dialog.run()
    dialog.destroy()

def main():
    cluster_configurator(None)

if __name__ == '__main__':
    main()
