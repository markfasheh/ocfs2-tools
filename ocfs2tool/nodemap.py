import gtk

import ocfs2

COLUMN_SLOT = 0
COLUMN_NAME = 1
COLUMN_IP = 2
COLUMN_PORT = 3
COLUMN_UUID = 4

class NodeMap:
    def __init__(self, device=None, advanced=False):
        self.device = device
        self.advanced = advanced

        info = self.info()

        if info:
            self.widget = gtk.ScrolledWindow()
            self.widget.set_policy(gtk.POLICY_AUTOMATIC, gtk.POLICY_AUTOMATIC)
            self.widget.add(info)
        else:
            self.widget = gtk.Label('Invalid device')

    def info(self):
        if not self.device:
            return None

        store = gtk.ListStore(int, str, str, int, str)

        tv = gtk.TreeView(store)
        
        tv.insert_column_with_attributes(-1, 'Slot #',
                                         gtk.CellRendererText(),
                                         text=COLUMN_SLOT)
        tv.insert_column_with_attributes(-1, 'Node Name',
                                         gtk.CellRendererText(),
                                         text=COLUMN_NAME)
        tv.insert_column_with_attributes(-1, 'IP Address',
                                         gtk.CellRendererText(),
                                         text=COLUMN_IP)
        tv.insert_column_with_attributes(-1, 'Port',
                                         gtk.CellRendererText(),
                                         text=COLUMN_PORT)

        if self.advanced:
            tv.insert_column_with_attributes(-1, 'UUID',
                                             gtk.CellRendererText(),
                                             text=COLUMN_UUID)

        return tv
def main():
    import sys

    window = gtk.Window()
    window.show()

    nodemap = NodeMap(sys.argv[1], False).widget
    window.add(nodemap)

    window.show_all()
    gtk.main()

if __name__ == '__main__':
    main()
