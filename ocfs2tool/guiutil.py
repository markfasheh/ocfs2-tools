import gtk

def set_props(obj, **kwargs):
    for k, v in kwargs.items():
        obj.set_property(k, v)

suffixes = ['K', 'MB', 'GB', 'TB']

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
                               buttons=gtk.BUTTONS_OK, message_format=msg)
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
