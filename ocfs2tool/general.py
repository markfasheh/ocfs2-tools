import gtk

import ocfs2

from guiutil import set_props, format_bytes

fields = (
    ('Version', 'version'),
    ('Label', 's_label'),
    ('UUID', 's_uuid'),
    ('Maximum Nodes', 's_max_nodes'),
    ('Cluster Size', 's_clustersize_bits'),
    ('Block Size', 's_blocksize_bits')
)

class General:
    def __init__(self, device=None, advanced=False):
        self.widget = gtk.Table(rows=5, columns=2)

        set_props(self.widget, row_spacing=4,
                               column_spacing=4,
                               border_width=4)

        super = None

        if device:
            try:
                super = ocfs2.get_super(device)
            except ocfs2.error:
                pass

        self.pos = 0

        for desc, member in fields:
            if super:
                if member == 'version':
                    val = '%d.%d' % super[0:2]
                else:
                    val = getattr(super, member)

                    if member.endswith('_bits'):
                        val = format_bytes(1 << val)
            else:
                val = 'N/A'

            self.add_field(desc, val)
        
    def add_field(self, desc, val):
        label = gtk.Label(desc + ':')
        set_props(label, xalign=1.0)
        self.widget.attach(label, 0, 1, self.pos, self.pos + 1,
                           xoptions=gtk.FILL, yoptions=gtk.FILL)

        label = gtk.Label(str(val))
        set_props(label, xalign=0.0)
        self.widget.attach(label, 1, 2, self.pos, self.pos + 1,
                           xoptions=gtk.FILL, yoptions=gtk.FILL)

        self.pos += 1

def main():
    import sys

    window = gtk.Window()

    general = General(sys.argv[1], False).widget
    window.add(general)

    window.show_all()

    gtk.main()

if __name__ == '__main__':
    main()
