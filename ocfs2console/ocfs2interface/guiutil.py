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

def set_props(obj, **kwargs):
    for k, v in kwargs.items():
        obj.set_property(k, v)

suffixes = ('K', 'MB', 'GB', 'TB')

def format_bytes(bytes, show_bytes=False):
    if bytes == 1:
        return '1 byte'
    elif bytes < 1024:
        return str(bytes) + ' bytes'

    fbytes = float(bytes)

    i = -1
    while i < 3 and fbytes >= 1024:
       fbytes /= 1024
       i += 1

    if show_bytes:
        return '%.1f %s (%sb)' % (fbytes, suffixes[i], bytes)
    else:
        return '%.0f %s' % (fbytes, suffixes[i])

def error_box(parent, msg):
    dialog = gtk.MessageDialog(parent=parent,
                               flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                               type=gtk.MESSAGE_ERROR,
                               buttons=gtk.BUTTONS_OK,
                               message_format=msg)
    dialog.run()
    dialog.destroy()

def query_text(parent, prompt):
    dialog = gtk.Dialog(parent=parent,
                        flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                        buttons=(gtk.STOCK_CANCEL, gtk.RESPONSE_CANCEL,
                                 gtk.STOCK_OK,     gtk.RESPONSE_OK))

    table = gtk.Table(rows=1, columns=2)
    set_props(table, row_spacing=4,
                     column_spacing=4,
                     border_width=4,
                     parent=dialog.vbox)

    label = gtk.Label(prompt + ':')
    set_props(label, xalign=1.0)
    table.attach(label, 0, 1, 0, 1)

    entry = gtk.Entry()
    table.attach(entry, 1, 2, 0, 1)

    dialog.show_all()

    if dialog.run() == gtk.RESPONSE_OK:
        text = entry.get_text()
    else:
        text = None

    dialog.destroy()

    return text


def make_callback(obj, callback, sub_callback):
    cb = getattr(obj, callback)

    if sub_callback:
        sub_cb = getattr(obj, sub_callback)

        def cb_func(*args):
            cb()
            sub_cb()
    else:
        def cb_func(*args):
            cb()

    return cb_func

if hasattr(gtk.Dialog, 'set_alternative_button_order'):
    Dialog = gtk.Dialog
else:
    class Dialog(gtk.Dialog):
        def set_alternative_button_order(self, new_order=None):
            pass
