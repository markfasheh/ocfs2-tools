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

PATH_FSTAB = '/etc/fstab'

class FSTab:
    def __init__(self):
        self.refresh()

    def refresh(self):
        self.entries = []

        try:
            fstab_file = open(PATH_FSTAB)
            lines = fstab_file.readlines()
            fstab_file.close()
        except IOError:
            return

        for line in lines:
            line = line.strip()

            if line.startswith('#'):
                continue

            try:
                entry = FSTabEntry(*line.split())
            except (ValueError, TypeError):
                continue

            self.entries.append(entry)

    def get(self, device=None, label=None, uuid=None):
        valid_specs = {}

        if device:
            valid_specs[device] = True

        if label:
            spec = 'LABEL=' + label
            valid_specs[spec] = True

        if uuid:
            spec = 'UUID=' + uuid.lower()
            valid_specs[spec] = True

        for entry in self.entries:
            if entry.spec in valid_specs:
                return entry

        return None
    
str_fields = ('spec', 'mountpoint', 'vfstype', 'options')
int_fields = ('freq', 'passno')

entry_fmt = ('\t'.join(['%%(%s)s' % f for f in str_fields]) + '\t' +
             '\t'.join(['%%(%s)d' % f for f in int_fields]))

class FSTabEntry:
    def __init__(self, spec, mountpoint, vfstype, options, freq=0, passno=0):
        symtab = locals()

        for attr in str_fields:
            setattr(self, attr, symtab[attr])

        for attr in int_fields:
            setattr(self, attr, int(symtab[attr]))

        if self.spec.startswith('UUID='):
            self.spec = 'UUID=' + self.spec[5:].lower()

    def __str__(self):
        return entry_fmt % self.__dict__

    def __repr__(self):
        return "<FSTabEntry: '%s'>" % str(self)

def main():
    import sys
    spec = sys.argv[1]

    fstab = FSTab()
    print fstab.get(device=spec, label=spec, uuid=spec)

if __name__ == '__main__':
    main()
