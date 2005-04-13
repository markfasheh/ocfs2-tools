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

import stat
import pwd
import grp
import time

import ocfs2

from classlabel import class_label

class Field(object):
    def __init__(self, dentry, dinode):
        self.dentry = dentry
        self.dinode = dinode

    def real_get_text(self):
        return str(getattr(self.dinode, self.dinode_member))

    def get_text(self):
        return self.real_get_text()

    text = property(get_text)

    label = class_label

    right_justify = False

file_type = {
    ocfs2.FT_UNKNOWN  : '?',
    ocfs2.FT_REG_FILE : '-',
    ocfs2.FT_DIR      : 'd',
    ocfs2.FT_CHRDEV   : 'c',
    ocfs2.FT_BLKDEV   : 'b',
    ocfs2.FT_FIFO     : 'p',
    ocfs2.FT_SOCK     : 's',
    ocfs2.FT_SYMLINK  : 'l',
}

class Mode(Field):
    def real_get_text(self):
        text = ['-'] * 10

        text[0] = file_type[self.dentry.file_type]

        mode = self.dinode.i_mode
        pos = 0

        for t in 'USR', 'GRP', 'OTH':
            for b in 'R', 'W', 'X':
                pos += 1

                if mode & getattr(stat, 'S_I%s%s' % (b, t)):
                    text[pos] = b.lower()

        pos = 0

        for t, b in (('UID', 'S'), ('GID', 'S'), ('VTX', 'T')):
            pos += 3

            if mode & getattr(stat, 'S_IS%s' % t):
                if text[pos] == 'x':
                    text[pos] = b
                else:
                    text[pos] = b.lower()

        return ''.join(text)

class Links(Field):
    label = '# Links'
    dinode_member = 'i_links_count'
    right_justify = True

class ID2Name(Field):
    def real_get_text(self):
        idnum = getattr(self.dinode, self.dinode_member)

        try:
            return self.get_name(idnum)[0]
        except KeyError:
            return str(idnum)

class Owner(ID2Name):
    dinode_member = 'i_uid'
    get_name = pwd.getpwuid

class Group(ID2Name):
    dinode_member = 'i_gid'
    get_name = grp.getgrgid

class Size(Field):
    dinode_member = 'i_size'
    right_justify = True

class AllocSize(Field):
    right_justify = True

    def real_get_text(self):
        return str(self.dinode.i_clusters * self.dinode.fs.fs_clustersize)

class Timestamp(Field):
    # Ported from GNU coreutils ls
    time_formats = ('%b %e  %Y', '%b %e %H:%M')

    def real_get_text(self):
        when = self.dinode.i_mtime
        when_local = time.localtime(when)

        current_time = long(time.time())

        six_months_ago = current_time - 31556952 / 2
        recent = (six_months_ago <= when and when < current_time)
        fmt = self.time_formats[recent]  

        return time.strftime(fmt, when_local)

class Name(Field):
    def real_get_text(self):
        return self.dentry.name
    
fields = (Mode, Links, Owner, Group, Size, AllocSize, Timestamp, Name)

def main():
    import sys

    fs = ocfs2.Filesystem(sys.argv[1])

    dentries = []

    def walk(dentry, offset, blocksize):
        dentries.append(dentry)

    fs.dir_iterate(walk)

    try:
        dentry = dentries[int(sys.argv[2])]
    except (IndexError, ValueError):
        dentry = dentries[0]

    dinode = fs.read_cached_inode(dentry.inode)

    for field_type in fields:
        field = field_type(dentry, dinode) 

        print '%s: %s' % (field.label, field.text)

if __name__ == '__main__':
    main()
