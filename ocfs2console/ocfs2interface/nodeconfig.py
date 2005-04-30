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

import os

import gtk
import gobject
import pango

import ocfs2
import o2cb

import o2cb_ctl

from guiutil import Dialog, set_props, error_box
from ipwidget import IPEditor, IPMissing, IPError

DEFAULT_CLUSTER_NAME = 'ocfs2'

PORT_DEFAULT = 7777
PORT_MINIMUM = 1000
PORT_MAXIMUM = 30000

(
    COLUMN_NEW_NODE,
    COLUMN_NAME,
    COLUMN_NODE,
    COLUMN_IP_ADDRESS,
    COLUMN_IP_PORT
) = range(5)

class Field:
    def __init__(self, name, column, title, widget_type, field_type):
        self.name = name
        self.column = column
        self.title = title
        self.widget_type = widget_type
        self.type = field_type

fields = (
    Field('new_node',   COLUMN_NEW_NODE,   'Active',     None,           bool),
    Field('name',       COLUMN_NAME,       'Name',       gtk.Entry,      str),
    Field('node',       COLUMN_NODE,       'Node',       None,           int),
    Field('ip_address', COLUMN_IP_ADDRESS, 'IP Address', IPEditor,       str),
    Field('ip_port',    COLUMN_IP_PORT,    'IP Port',    gtk.SpinButton, str),
)

# Hate your ancient distros shipping old pygtks
typemap = { bool: gobject.TYPE_BOOLEAN }

class ConfigError(Exception):
    pass

class ClusterConfig(Dialog):
    def __init__(self, cluster_name=DEFAULT_CLUSTER_NAME, parent=None):
        self.cluster_name = cluster_name

        Dialog.__init__(self, parent=parent, title='Node Configuration',
                        buttons=(gtk.STOCK_APPLY, gtk.RESPONSE_APPLY,
                                 gtk.STOCK_CLOSE, gtk.RESPONSE_CLOSE))

        if parent is None:
            self.set_type_hint(gtk.gdk.WINDOW_TYPE_HINT_NORMAL)

        frame = gtk.Frame()
        frame.set_shadow_type(gtk.SHADOW_NONE)
        self.vbox.add(frame)

        label = gtk.Label()
        label.set_text_with_mnemonic('_Nodes:')
        frame.set_label_widget(label)

        hbox = gtk.HBox(spacing=4)
        hbox.set_border_width(4)
        frame.add(hbox)

        scrl_win = gtk.ScrolledWindow()     
        scrl_win.set_policy(hscrollbar_policy=gtk.POLICY_AUTOMATIC,
                            vscrollbar_policy=gtk.POLICY_AUTOMATIC)
        hbox.pack_start(scrl_win)

        self.setup_treeview()
        label.set_mnemonic_widget(self.tv)
        scrl_win.add(self.tv)

        vbbox = gtk.VButtonBox()
        set_props(vbbox, layout_style=gtk.BUTTONBOX_START,
                         spacing=5,
                         border_width=5)
        hbox.pack_end(vbbox, expand=False, fill=False)

        self.add_button = gtk.Button(stock=gtk.STOCK_ADD)
        self.add_button.connect('clicked', self.add_node)
        vbbox.add(self.add_button)

        try:
            edit_construct_args = {'stock': gtk.STOCK_EDIT}
        except AttributeError:
            edit_construct_args = {'label': '_Edit'}

        self.edit_button = gtk.Button(**edit_construct_args)
        self.edit_button.connect('clicked', self.edit_node)
        vbbox.add(self.edit_button)
        
        self.remove_button = gtk.Button(stock=gtk.STOCK_REMOVE)
        self.remove_button.connect('clicked', self.remove_node)
        vbbox.add(self.remove_button)

        self.can_edit(False)
        self.can_apply(False)
 
        self.sel = self.tv.get_selection()
        self.sel.connect('changed', self.on_select)

        self.load_cluster_state()

    def load_cluster_state(self):
        try:
            nodes = o2cb_ctl.get_cluster_nodes(self.cluster_name, self)
        except o2cb_ctl.CtlError, e:
            raise ConfError, str(e)

        store = gtk.ListStore(*[typemap.get(f.type, f.type) for f in fields])

        node_data = list((None,) * len(fields))

        for node in nodes:
            for field in fields:
                val = node.get(field.name, False)
                node_data[field.column] = field.type(val)

            store.append(node_data)

        def node_compare(store, a, b):
            n1 = store[a][COLUMN_NODE]
            n2 = store[b][COLUMN_NODE]

            if n1 < 0 and n2 >= 0:
                return 1
            elif n1 >= 0 and n2 < 0:
                return -1
            else:
                return cmp(abs(n1), abs(n2))
              
        store.set_sort_func(COLUMN_NODE, node_compare)
        store.set_sort_column_id(COLUMN_NODE, gtk.SORT_ASCENDING)

        self.new_nodes = 0
        self.store = store

        self.tv.set_model(store)

        if len(store):
            self.sel.select_iter(self.store.get_iter_first())

    def setup_treeview(self):
        self.tv = gtk.TreeView()
        self.tv.set_size_request(350, 200)

        for field in fields:
            if field.column == COLUMN_NEW_NODE:
                self.tv.insert_column_with_data_func(-1, field.title,
                                                     gtk.CellRendererPixbuf(),
                                                     self.active_set_func)
            elif field.column == COLUMN_NODE:
                self.tv.insert_column_with_data_func(-1, field.title,
                                                     gtk.CellRendererText(),
                                                     self.node_set_func)
            else:
                cell_renderer = gtk.CellRendererText()
                cell_renderer.set_property('style', pango.STYLE_ITALIC)

                self.tv.insert_column_with_attributes(-1, field.title,
                                                      cell_renderer,
                                                      text=field.column,
                                                      style_set=COLUMN_NEW_NODE)

    def active_set_func(self, tree_column, cell, model, iter):
        if model[iter][COLUMN_NEW_NODE]:
            stock_id = None
        else:
            stock_id = gtk.STOCK_EXECUTE

        cell.set_property('stock_id', stock_id)

    def node_set_func(self, tree_column, cell, model, iter):
        if model[iter][COLUMN_NEW_NODE]:
            text = ''
        else:
            text = str(model[iter][COLUMN_NODE])

        cell.set_property('text', text)

    def on_select(self, sel):
        store, iter = sel.get_selected()

        if iter:
            editable = store[iter][COLUMN_NEW_NODE]
        else:
            editable = False

        self.can_edit(editable)

    def can_edit(self, state):
        self.edit_button.set_sensitive(state)
        self.remove_button.set_sensitive(state)

    def can_apply(self, state):
        self.set_response_sensitive(gtk.RESPONSE_APPLY, state)
            
    def add_node(self, b):
        if len(self.store) == ocfs2.MAX_NODES:
            error_box(self, 'Cannot have more than %d nodes in a cluster' %
                            ocfs2.MAX_NODES)
            return

        node_attrs = self.node_query(title='Add Node')

        if node_attrs is None:
            return

        self.new_nodes += 1

        name, ip_addr, ip_port = node_attrs

        iter = self.store.append((True, name, -self.new_nodes,
                                  ip_addr, ip_port))
        self.sel.select_iter(iter)

        self.can_apply(True)

    def edit_node(self, b):
        store, iter = self.sel.get_selected()
        attrs = store[iter]

        node_attrs = self.node_query(title='Edit Node', defaults=attrs)

        if node_attrs is None:
            return

        (attrs[COLUMN_NAME],
         attrs[COLUMN_IP_ADDRESS],
         attrs[COLUMN_IP_PORT]) = node_attrs

    def remove_node(self, b):
        store, iter = self.sel.get_selected()

        msg = ('Are you sure you want to delete node %s?' %
               store[iter][COLUMN_NAME])

        ask = gtk.MessageDialog(parent=self,
                                flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                                type=gtk.MESSAGE_QUESTION,
                                buttons=gtk.BUTTONS_YES_NO,
                                message_format=msg)

        response = ask.run()
        ask.destroy()

        if response == gtk.RESPONSE_YES:
            del store[iter]
            self.new_nodes -= 1

            if self.new_nodes == 0:
                self.can_apply(False)

            self.sel.select_iter(self.store.get_iter_first())

    def new_node_attrs(self):
        attrs = []

        for row in self.store:
            if row[COLUMN_NEW_NODE]:
                attrs.append((row[COLUMN_NAME],
                              row[COLUMN_IP_ADDRESS],
                              row[COLUMN_IP_PORT]))

        return attrs

    def apply_changes(self):
        success = False

        for name, ip_address, ip_port in self.new_node_attrs():
            success, output, k = o2cb_ctl.add_node(name, self.cluster_name,
                                                   ip_address, ip_port, self)
            if not success:
                error_box(self, '%s\nCould not add node %s' % (output, name))
                break

        self.load_cluster_state()
        return success

    def node_query(self, title='Node Attributes', defaults=None):
        existing_names = {}
        existing_ip_addrs = {}

        for row in self.store:
            name = row[COLUMN_NAME]
            ip_addr = row[COLUMN_IP_ADDRESS]

            existing_names[name] = 1
            existing_ip_addrs[ip_addr] = name

        dialog = Dialog(parent=self, title=title,
                        buttons=(gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL,
                                 gtk.STOCK_OK,     gtk.RESPONSE_OK))

        dialog.set_alternative_button_order((gtk.RESPONSE_OK,
                                             gtk.RESPONSE_CANCEL))

        dialog.set_default_response(gtk.RESPONSE_OK)

        table = gtk.Table(rows=4, columns=2)
        set_props(table, row_spacing=4,
                         column_spacing=4,
                         border_width=4,
                         parent=dialog.vbox)

        widgets = []

        for row, field in enumerate(fields):
            if field.widget_type is None:
                widgets.append(None)
                continue

            label = gtk.Label(field.title + ':')
            set_props(label, xalign=1.0)
            table.attach(label, 0, 1, row, row + 1)

            widget = field.widget_type()
            table.attach(widget, 1, 2, row, row + 1)

            if field.column == COLUMN_NAME:
                widget.set_max_length(o2cb.NM_MAX_NAME_LEN)
            elif field.column == COLUMN_IP_PORT:
                widget.set_numeric(True)

                adjustment = gtk.Adjustment(PORT_DEFAULT,
                                            PORT_MINIMUM, PORT_MAXIMUM,
                                            1, 100) 

                widget.set_adjustment(adjustment)
                widget.set_value(PORT_DEFAULT)

            widgets.append(widget)

        if defaults:
            for w, d in zip(widgets, defaults):
                if w and d:
                    w.set_text(d)

        dialog.show_all()

        while 1:
            if dialog.run() != gtk.RESPONSE_OK:
                dialog.destroy()
                return None

            ip_port = widgets[COLUMN_IP_PORT].get_text()

            name = widgets[COLUMN_NAME].get_text()

            if not name:
                error_box(dialog, 'Node name not specified')
                continue

            try:
                ip_addr = widgets[COLUMN_IP_ADDRESS].get_text()
            except (IPMissing, IPError), msg:
                error_box(dialog, msg[0])
                continue

            if name in existing_names:
                error_box(dialog,
                          'Node %s already exists in the configuration' % name)
            elif ip_addr in existing_ip_addrs:
                error_box(dialog,
                          'IP %s is already assigned to node %s' %
                          (ip_addr, existing_ip_addrs[ip_addr]))
            else:
                break

        dialog.destroy()

        return name, ip_addr, ip_port

    def run(self):
        self.show_all()

        while 1:
            if Dialog.run(self) == gtk.RESPONSE_APPLY:
                self.apply_changes()
            elif len(self.new_node_attrs()):
                msg = ('New nodes have been created, but they have not been '
                       'applied to the cluster configuration. Do you want to '
                       'apply the changes now?')

                ask = gtk.MessageDialog(parent=self,
                                        flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                                        type=gtk.MESSAGE_QUESTION,
                                        buttons=gtk.BUTTONS_YES_NO,
                                        message_format=msg)

                if ask.run() == gtk.RESPONSE_NO:
                    break
                elif self.apply_changes():
                    break
            else: 
                break

def node_config(parent=None):
    if not os.access(o2cb.FORMAT_CLUSTER_DIR, os.F_OK):
        success, output, k = o2cb_ctl.init_load(parent)

        if success:
            msg_type = gtk.MESSAGE_INFO
            msg = ('The cluster stack has been started. It needs to be '
                   'running for any clustering functionality to happen. '
                   'Please run "%s enable" to have it started upon bootup.'
                   % o2cb_ctl.O2CB_INIT)
        else:
            msg_type = gtk.MESSAGE_WARNING
            msg = ('Could not start cluster stack. This must be resolved '
                   'before any OCFS2 filesystem can be mounted')

        info = gtk.MessageDialog(parent=parent,
                                 flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                                 type=msg_type,
                                 buttons=gtk.BUTTONS_CLOSE,
                                 message_format=msg)

        info.run()
        info.destroy()

        if not success:
            return

    try:
        cluster_name = o2cb_ctl.get_active_cluster_name(parent)
    except o2cb_ctl.CtlError, e:
        error_box(parent, str(e))
        return

    try:
        conf = ClusterConfig(cluster_name, parent)
    except ConfigError, e:
        error_box(parent, '%s: Could not query cluster configuration' % str(e))
        return

    conf.run()
    conf.destroy()

    if not os.access(o2cb.FORMAT_CLUSTER % cluster_name, os.F_OK):
        success, output, k = o2cb_ctl.init_online(cluster_name, parent)

        if not success:
            msg = ('Could not bring OCFS2 cluster online. This must be '
                   'resolved before any OCFS2 filesystem can be mounted')

            info = gtk.MessageDialog(parent=parent,
                                     flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                                     type=gtk.MESSAGE_WARNING,
                                     buttons=gtk.BUTTONS_CLOSE,
                                     message_format=msg)

            info.run()
            info.destroy()

def main():
    from about import process_gui_args
    process_gui_args()
    node_config()

if __name__ == '__main__':
    main()
