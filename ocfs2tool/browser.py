import gtk

import ocfs2

from guiutil import set_props

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

         table = gtk.Table(rows=4, columns=2)
         set_props(table, row_spacing=4,
                          column_spacing=4,
                          border_width=4)
         self.widget.pack_end(table)
