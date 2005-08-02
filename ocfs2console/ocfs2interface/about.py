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

import sys

OCFS2TOOL_VERSION = '1.1.0'

def print_version():
    print 'OCFS2Console version %s' % OCFS2TOOL_VERSION

def print_usage(name):
    print '''Usage: %s [OPTION]...
Options:
  -N, --node-config  run node configurator only
  -V, --version      print version information and exit
      --help         display this help and exit''' % name

def process_args():
    nodeconf = False

    for arg in sys.argv[1:]:
        if arg in ('--version', '-V'):
            print_version()
            sys.exit(0)
        elif arg in ('--help',):
            print_usage(sys.argv[0])
            sys.exit(0)
        elif arg in ('--node-config', '-N'):
            nodeconf = True

    return nodeconf

def process_gui_args():
    if len(sys.argv) > 1 and sys.argv[1] not in ('--clusterconf', '-C'):
        print_usage(sys.argv[0])
        sys.exit(1)
        
def about(parent):
    import gtk

    from guiutil import set_props

    if gtk.pygtk_version >= (2,6,0):
        copyright = 'Copyright (C) 2002, 2005 Oracle.  All rights reserved.'
        license = '''
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public
License along with this program; if not, write to the Free
Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.
'''
        blurb = 'GUI frontend for OCFS2 management'

        #logo = gtk.gdk.pixbuf_new_from_file('logo.png')

        dialog = gtk.AboutDialog()
        dialog.set_transient_for(parent)
        dialog.set_destroy_with_parent(True)

        set_props(dialog, name='OCFS2 Console',
                          version=OCFS2TOOL_VERSION,
                          copyright=copyright,
                          license=license,
                          website='http://oss.oracle.com',
                          comments=blurb)
                          #logo=logo)

    else:
        dialog = gtk.MessageDialog(parent=parent,
                                   flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                                   buttons=gtk.BUTTONS_CLOSE)
        dialog.label.set_text(
'''OCFS2 Console 
Version %s
Copyright (C) 2002, 2005 Oracle.
All Rights Reserved.''' % OCFS2TOOL_VERSION)

    dialog.run()
    dialog.destroy()

def main():
    process_args()
    process_gui_args()
    about(None)

if __name__ == '__main__':
    main()
