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

import gtk
import gobject
import pango

import ocfs2

import gidle

from guiutil import set_props

from ls import fields

INFO_LABEL_FONT = pango.FontDescription('monospace')

(
    COLUMN_NAME,
    COLUMN_INFO_OBJECT,
    COLUMN_ICON,
    COLUMN_ITALIC
) = range(4)

STOCK_LOADING = gtk.STOCK_REFRESH
STOCK_EMPTY = gtk.STOCK_STOP
STOCK_ERROR = gtk.STOCK_DIALOG_ERROR

try:
    STOCK_FILE = gtk.STOCK_FILE
except AttributeError:
    STOCK_FILE = gtk.STOCK_NEW

try:
    STOCK_DIRECTORY = gtk.STOCK_DIRECTORY
except AttributeError:
    STOCK_DIRECTORY = gtk.STOCK_OPEN

INVALID_DENTRY = 'poop'

class InfoLabel(gtk.Label):
    def __init__(self, field_type):
        gtk.Label.__init__(self)
        self.set_selectable(True)

        self.field_type = field_type

        if field_type.right_justify:
             set_props(self, xalign=1.0)
        else:
             set_props(self, xalign=0.0)

        self.modify_font(INFO_LABEL_FONT)

        if hasattr(field_type, 'width_chars'):
            context = self.get_pango_context()

            desc = INFO_LABEL_FONT.copy()
            desc.set_size(context.get_font_description().get_size())

            metrics = context.get_metrics(desc, context.get_language())

            char_width = metrics.get_approximate_char_width()
            digit_width = metrics.get_approximate_digit_width()
            char_pixels = pango.PIXELS(max(char_width, digit_width))

            self.set_size_request(char_pixels * field_type.width_chars, -1)

    def update(self, dentry, dinode):
        field = self.field_type(dentry, dinode)
        self.set_text(field.text)

    def clear(self):
        self.set_text('')

class Browser(gtk.VBox):
    def __init__(self, device=None):
        self.device = device

        gtk.VBox.__init__(self, spacing=4)

        label = gtk.Label('/')
        set_props(label, xalign=0.0,
                         selectable=True,
                         wrap=True)
        self.pack_start(label, expand=False)

        self.path_label = label

        self.scrl_win = gtk.ScrolledWindow()
        self.scrl_win.set_policy(gtk.POLICY_AUTOMATIC, gtk.POLICY_AUTOMATIC)
        self.add(self.scrl_win)

        self.make_dentry_store()
        self.make_file_view()
        self.make_ls_fields()

        self.connect('destroy', self.destroy_handler)

        self.refresh()

    def make_dentry_store(self):
        def tree_compare(store, a, b):
            d1 = self.get_dentry(store, a)
            d2 = self.get_dentry(store, b)

            if d1 is d2:
                return 0
            elif d1 is INVALID_DENTRY:
                return 1
            elif d2 is INVALID_DENTRY:
                return -1
            elif d1 and not d2:
                return 1
            elif not d1 and d2:
                return -1
            elif d1.file_type != d2.file_type:
                if d1.file_type == ocfs2.FT_DIR:
                    return -1
                elif d2.file_type == ocfs2.FT_DIR:
                    return 1
                else:
                    return cmp(d1.name, d2.name)
            else:
                return cmp(d1.name, d2.name)

        self.store = gtk.TreeStore(str, gobject.TYPE_PYOBJECT,
                                   str, gobject.TYPE_BOOLEAN)

        self.store.set_sort_func(COLUMN_NAME, tree_compare)
        self.store.set_sort_column_id(COLUMN_NAME, gtk.SORT_ASCENDING)

    def make_file_view(self):
        tv = gtk.TreeView(self.store)
        self.scrl_win.add(tv)

        set_props(tv, headers_visible=False)

        column = gtk.TreeViewColumn()

        renderer = gtk.CellRendererPixbuf()
        column.pack_start(renderer, expand=False)
        column.set_attributes(renderer, stock_id=COLUMN_ICON)

        renderer = gtk.CellRendererText()
        renderer.set_property('style', pango.STYLE_ITALIC)
        column.pack_start(renderer, expand=True)
        column.set_attributes(renderer, text=COLUMN_NAME,
                                        style_set=COLUMN_ITALIC)

        tv.append_column(column)

        tv.connect('test_expand_row', self.tree_expand_row)
        tv.connect('test_collapse_row', self.tree_collapse_row)

        sel = tv.get_selection()
        sel.connect('changed', self.select_dentry)

    def make_ls_fields(self):
        table = gtk.Table(rows=2, columns=7)
        set_props(table, row_spacing=4,
                         column_spacing=4,
                         border_width=4)
        self.pack_end(table, expand=False, fill=False)

        self.info_labels = []

        for column, field in enumerate(fields):
            label = gtk.Label(field.label)

            if field.right_justify:
                set_props(label, xalign=1.0)
            else:
                set_props(label, xalign=0.0)

            xoptions = yoptions = gtk.FILL
            xpadding = 2

            table.attach(label, column, column + 1, 0, 1,
                         xoptions, yoptions, xpadding)

            label = InfoLabel(field)
            table.attach(label, column, column + 1, 1, 2,
                         xoptions, yoptions, xpadding)

            self.info_labels.append(label)

    def destroy_handler(self, obj):
        self.cleanup()

    def make_dentry_node(self, dentry, stock_id, parent=None):
        return self.store.append(parent, (dentry.name, dentry, stock_id, False))

    def make_file_node(self, dentry, parent=None):
        self.make_dentry_node(dentry, STOCK_FILE, parent)

    def make_dir_node(self, dentry, parent=None):
        iter = self.make_dentry_node(dentry, STOCK_DIRECTORY, parent)
        self.store.append(iter, ('.', dentry, None, False))

    def make_loading_node(self, parent=None):
        self.store.append(parent, ('Loading...', None, STOCK_LOADING, True))

    def make_empty_node(self, parent=None):
        self.store.append(parent, ('Empty', None, STOCK_EMPTY, True))

    def make_error_node(self, parent=None):
        self.store.append(parent, ('Error', INVALID_DENTRY, STOCK_ERROR, True))

    def cleanup(self):
        if hasattr(self, 'levels'):
            for level in self.levels:
                level.destroy()

        self.levels = []

    def refresh(self):
        self.cleanup()

        self.store.clear()

        self.fs = None

        if self.device:
            try:
                self.fs = ocfs2.Filesystem(self.device)
            except ocfs2.error:
                self.make_error_node()

            if self.fs:
                self.add_level()
        else:
            self.make_empty_node()

    def add_level(self, dentry=None, parent=None):
        if parent:
            iter = self.store.iter_children(parent)

            name = self.store[iter][COLUMN_NAME]
            if name != '.':
                return

            del self.store[iter]

        try:
            diriter = self.fs.iterdir(dentry)
        except ocfs2.error:
            self.make_error_node(parent)
            return

        self.make_loading_node(parent)

        level = TreeLevel(diriter, dentry, parent)
        self.levels.append(level)

        if parent: 
            self.store[parent][COLUMN_INFO_OBJECT] = level
      
        level.set_callback(self.populate_level, level)
        level.attach()

    def populate_level(self, level):
        try:
            dentry = level.diriter.next()
        except (StopIteration, ocfs2.error), e:
             self.destroy_level(level, isinstance(e, ocfs2.error))
             return False

        if dentry.file_type == ocfs2.FT_DIR:
            self.make_dir_node(dentry, level.parent)
        else:
            self.make_file_node(dentry, level.parent)

        return True

    def destroy_level(self, level, error=False):
        if error:
            self.make_error_node(level.parent)
        else:
            children = self.store.iter_n_children(level.parent)

            if children < 2:
                self.make_empty_node(level.parent)

        if level.parent:
            self.store[level.parent][COLUMN_INFO_OBJECT] = level.dentry

            # Argh, ancient pygtk can't handle None being passed to
            # iter_children
            iter = self.store.iter_children(level.parent)
        else:
            iter = self.store.get_iter_first()

        self.store.remove(iter)

        self.levels.remove(level)

        del level.diriter

    def tree_expand_row(self, tv, iter, path):
        info_obj = self.store[iter][COLUMN_INFO_OBJECT]

        if isinstance(info_obj, TreeLevel):
            level.collapsed = False
            level.foreground(level)
        else:
            self.add_level(info_obj, iter)

    def tree_collapse_row(self, tv, iter, path):
        info_obj = self.store[iter][COLUMN_INFO_OBJECT]

        if isinstance(info_obj, TreeLevel):
            level = info_obj

            level.collapsed = True
            level.background()

    def select_dentry(self, sel):
        store, iter = sel.get_selected()

        if store and iter:
            dentry = self.get_dentry(store, iter)
        else:
            dentry = None

        if dentry:
            self.display_dentry(dentry)
        else:
            self.display_clear()
            if iter:
                iter = store.iter_parent(iter)

        self.path_label.set_text(self.get_fs_path(store, iter))

    def display_dentry(self, dentry):
        dinode = self.fs.read_cached_inode(dentry.inode)

        for label in self.info_labels:
            label.update(dentry, dinode)

    def display_clear(self):
        for label in self.info_labels:
            label.clear()

    def get_dentry(self, store, iter):
        info_obj = store[iter][COLUMN_INFO_OBJECT]

        if isinstance(info_obj, ocfs2.DirEntry):
            return info_obj
        elif isinstance(info_obj, TreeLevel):
            return info_obj.dentry
        else:
            return None

    def get_fs_path(self, store, iter):
        parts = []

        while iter:
            dentry = self.get_dentry(store, iter)
            parts.append(dentry.name)
            iter = store.iter_parent(iter)

        parts.reverse()

        return '/' + '/'.join(parts)

class TreeLevel(gidle.Idle):
    def __init__(self, diriter, dentry=None, parent=None):
        gidle.Idle.__init__(self)

        self.diriter = diriter
        self.dentry = dentry

        if parent:
            self.parent = parent.copy()
        else:
            self.parent = None

        self.collapsed = False

    def foreground(self):
        if not self.collapsed:
            self.set_priority(gobject.PRIORITY_DEFAULT_IDLE)

    def background(self):
        level.set_priority(gobject.PRIORITY_LOW)

def main():
    import sys

    def dummy(*args):
        gtk.main_quit()

    window = gtk.Window()
    window.set_default_size(400, 300)
    window.connect('delete_event', dummy)

    browser = Browser(sys.argv[1])
    window.add(browser)

    window.show_all()

    gtk.main()

if __name__ == '__main__':
    main()
