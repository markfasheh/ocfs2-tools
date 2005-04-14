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

import types

from cStringIO import StringIO

from process import Process
from guiutil import error_box

DEFAULT_CLUSTER_NAME = 'ocfs2'

O2CB_INIT = '/etc/init.d/o2cb'
O2CB_CTL = 'o2cb_ctl'

class CtlError(Exception):
    pass

class O2CBProcess(Process):
    def __init__(self, args, desc, parent=None):
        if isinstance(args, types.StringTypes):
            command = '%s %s' % (self.o2cb_program, args)
        else:
            command = (self.o2cb_program,) + tuple(args)

        Process.__init__(self, command, self.o2cb_title, desc, parent)

class O2CBCtl(O2CBProcess):
    o2cb_program = O2CB_CTL
    o2cb_title = 'Cluster Control'

class O2CBInit(O2CBProcess):
    o2cb_program = O2CB_INIT
    o2cb_title = 'Cluster Stack'

def init_load(parent=None):
    args = ('load',)
    o2cb_init = O2CBInit(args, 'Starting cluster stack...', parent)
    return o2cb_init.reap()

def init_online(cluster_name, parent=None):
    desc = 'Starting %s cluster...' % cluster_name
    args = ('online', cluster_name)
    o2cb_init = O2CBInit(args, desc, parent)
    return o2cb_init.reap()

def query_clusters(parent=None):
    args = '-I -t cluster -o'
    o2cb_ctl = O2CBCtl(args, 'Querying cluster...', parent)
    return o2cb_ctl.reap()

def query_nodes(parent=None):
    args = '-I -t node -o'
    o2cb_ctl = O2CBCtl(args, 'Querying nodes...', parent)
    return o2cb_ctl.reap()

def add_node(name, cluster_name, ip_address, ip_port, parent=None):
    desc = 'Adding node %s...' % name
    args = ('-C', '-n', name, '-t', 'node',
            '-a', 'cluster=%s' % cluster_name,
            '-a', 'ip_address=%s' % ip_address,
            '-a', 'ip_port=%s' % ip_port,
            '-i')

    o2cb_ctl = O2CBCtl(args, desc, parent)
    return o2cb_ctl.reap()

def get_active_cluster_name(parent=None):
    cluster_name = None

    success, output, k = query_clusters(parent)

    if success:
        names = []

        buffer = StringIO(output)

        for line in buffer:
            if line.startswith('#'):
                continue

            try:
                name, rest = line.split(':', 1)
            except ValueError:
                 continue

            names.append(name)

        if DEFAULT_CLUSTER_NAME in names:
            cluster_name = DEFAULT_CLUSTER_NAME
        elif len(names):
            cluster_name = names[0]

    if cluster_name is None:
        args = '-C -n %s -t cluster -i' % DEFAULT_CLUSTER_NAME
        o2cb_ctl = O2CBCtl(args, 'Creating cluster...', parent)
        success, output, k = o2cb_ctl.reap()

        if success:
            cluster_name = DEFAULT_CLUSTER_NAME
        else:
            errmsg = '%s\nCould not create cluster' % (output,
                                                       DEFAULT_CLUSTER_NAME)
            raise CtlError, errmsg

    return cluster_name

def get_cluster_nodes(cluster_name, parent=None):
    success, output, k = query_nodes(parent)
    if not success:
        raise CtlError, output

    nodes = []

    buffer = StringIO(output)

    for line in buffer:
        if line.startswith('#'):
            continue

        try:
            name, cluster, node, ip_address, ip_port, state = line.split(':')
        except ValueError:
            continue

        if cluster == cluster_name:
            info = {}
            symtab = locals()

            for sym in 'name', 'node', 'ip_address', 'ip_port':
                info[sym] = symtab[sym]

            nodes.append(info)

    return nodes

def main():
    success, output, k = init_load()
    if success:
        print 'Success:\n' + output
    else:
        print 'Failed:\n' + output

    success, output, k = query_nodes()
    if success:
        print 'Success:\n' + output
    else:
        print 'Failed:\n' + output

if __name__ == '__main__':
    main()
