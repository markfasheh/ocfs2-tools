import gtk

import ocfs2

class Bitmap:
    def __init__(self, device=None, advanced=False):
        self.device = device

        info = self.info()

        if info:
            self.widget = gtk.ScrolledWindow()
            self.widget.set_policy(gtk.POLICY_AUTOMATIC, gtk.POLICY_AUTOMATIC)
            self.widget.set_border_width(4)
            self.widget.add(info)
        else:
            self.widget = gtk.Label('Invalid device')

    def info(self):
        if not self.device:
            return None

        bitmap = ocfs2.Bitmap('abc', 8)
        return ocfs2.CellMap(bitmap)
