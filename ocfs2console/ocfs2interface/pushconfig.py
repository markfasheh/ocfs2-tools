# OCFS2Console - GUI frontend for OCFS2 management and debugging
# Copyright (C) 2005 Oracle.  All rights reserved.
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

import socket

import gtk

import o2cb_ctl

from guiutil import error_box

from terminal import TerminalDialog, terminal_ok as pushconfig_ok

CONFIG_FILE = '/etc/ocfs2/cluster.conf'

command_template = '''set -e
mkdir -p /etc/ocfs2
cat > /etc/ocfs2/cluster.conf <<\_______EOF
%(cluster_config)s
_______EOF
#/etc/init.d/o2cb online %(cluster_name)s
'''

def get_hosts(parent=None):
    hostname = socket.gethostname()

    cluster_name = o2cb_ctl.get_active_cluster_name(parent)

    nodes = o2cb_ctl.get_cluster_nodes(cluster_name, parent)

    remote_nodes = [node['name'] for node in nodes if node['name'] != hostname]

    return cluster_name, remote_nodes

def generate_command(cluster_name):
    conf_file = open(CONFIG_FILE)
    config_data = conf_file.read()
    conf_file.close()

    if config_data.endswith('\n'):
        config_data = config_data[:-1]

    info = {'cluster_config' : config_data,
            'cluster_name'   : cluster_name}

    return command_template % info

def propagate(terminal, dialog, remote_command, host_iter):
    try:
        host = host_iter.next()
    except StopIteration:
        terminal.feed('Finished!\r\n', -1)
        dialog.finished = True
        return

    command = ('ssh', 'root@%s' % host, remote_command)

    terminal.feed('Propagating cluster configuration to %s...\r\n' % host, -1)
    terminal.fork_command(command=command[0], argv=command)

def push_config(parent=None):
    try:
        cluster_name, hosts = get_hosts(parent)
    except o2cb_ctl.CtlError, e:
        error_box(parent, str(e))
        return

    try:
        command = generate_command(cluster_name)
    except IOError, e:
        error_box(parent, str(e))
        return

    title = 'Propagate Cluster Configuration'

    dialog = TerminalDialog(parent=parent, title=title)
    terminal = dialog.terminal

    dialog.finished = False
    dialog.show_all()

    host_iter = iter(hosts)
    terminal.connect('child-exited', propagate, dialog, command, host_iter)

    propagate(terminal, dialog, command, host_iter)

    while 1:
        dialog.run()

        if dialog.finished:
            break

        msg = ('Cluster configuration propagation is still running. You '
               'should not close this window until it is finished')

        info = gtk.MessageDialog(parent=dialog,
                                 flags=gtk.DIALOG_DESTROY_WITH_PARENT,
                                 type=gtk.MESSAGE_WARNING,
                                 buttons=gtk.BUTTONS_CLOSE,
                                 message_format=msg)
        info.run()
        info.destroy()
 
    dialog.destroy()

def main():
    push_config()

if __name__ == '__main__':
    main()
