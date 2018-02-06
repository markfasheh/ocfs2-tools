#
# class to create an IP address entry widget and to sanity check entered values
#
# Jonathan Blandford <jrb@redhat.com>
# Michael Fulbright <msf@redhat.com>
#
# Copyright 2002 Red Hat, Inc.
#
# This software may be freely redistributed under the terms of the GNU
# library public license.
#
# You should have received a copy of the GNU Library Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
#
# Modified by Manish Singh <manish.singh@oracle.com>

import re
import string
import gtk
import gobject

# we're not gettexted - Manish
def _(text):
    return text

# The below is from network.py from anaconda
#
# Matt Wilson <ewt@redhat.com>
# Erik Troan <ewt@redhat.com>
# Mike Fulbright <msf@redhat.com>
# Brent Fox <bfox@redhat.com>
#
# Copyright 2001-2003 Red Hat, Inc.
#
# sanity check an IP string.  if valid, returns octets, if invalid, return None
def sanityCheckIPString(ip_string):
    ip_re = re.compile('^([0-2]?[0-9]?[0-9])\\.([0-2]?[0-9]?[0-9])\\.([0-2]?[0-9]?[0-9])\\.([0-2]?[0-9]?[0-9])$')
    
    #Sanity check the string
    m = ip_re.match (ip_string)
    try:
        if not m:
            return None
        octets = m.groups()
        if len(octets) != 4:
            return None
        for octet in octets:
            if (int(octet) < 0) or (int(octet) > 255):
                return None
    except TypeError:
        return None

    return octets

ip_fields = ['entry1', 'entry2', 'entry3', 'entry4']

# Includes an error message, and the widget with problems
class IPError(Exception):
    pass

class IPMissing(Exception):
    pass

class IPEditor(gtk.HBox):
    def __init__ (self):
        gtk.HBox.__init__(self)

	self.entrys = {}
	for name in ip_fields:
	    self.entrys[name] = gtk.Entry(3)
	    self.entrys[name].set_size_request(50,-1)
	    self.entrys[name].set_max_length(3)
	    self.entrys[name].set_activates_default(True)

	for i in range(0, len(ip_fields)):
	    name = ip_fields[i]
	    if name != 'entry4':
		nname = self.entrys[ip_fields[i+1]]
	    else:
		nname = None

	    self.entrys[name].connect('insert_text', self.entry_insert_text_cb, nname)

	for name in ip_fields:
	    self.pack_start(self.entrys[name], False, False)
	    if name != 'entry4':
		self.pack_start(gtk.Label('.'), False, False)

    def getFocusableWidget(self):
        return self.entrys['entry1']
	
    def clear_entries (self):
	for name in ip_fields:
	    self.entrys[name].set_text('')
        
    def hydrate (self, ip_string):
        self.clear_entries()

        octets = sanityCheckIPString(ip_string)
        if octets is None:
            return

	i = 0
	for name in ip_fields:
	    self.entrys[name].set_text(octets[i])
	    i = i + 1

    def dehydrate (self):
        widget = None
	# test if empty
	empty = 1
	for e in ['entry1', 'entry2', 'entry3', 'entry4']:
	    if len(string.strip(self.entrys[e].get_text())) > 0:
		empty = 0
		break

	if empty:
	    raise IPMissing, (_("IP Address is missing"), widget)
	    
        try:
            widget = self.entrys['entry1']
            if int(widget.get_text()) > 255 or int(widget.get_text()) <= 0:
                raise IPError, (_("IP Addresses must contain numbers between 1 and 255"), widget)                    
            
            for ent in ['entry2', 'entry3', 'entry4']:
                widget = self.entrys[ent]
                if int(widget.get_text()) > 255:
                    raise IPError, (_("IP Addresses must contain numbers between 0 and 255"), widget)                    
        except ValueError, msg:
            raise IPError, (_("IP Addresses must contain numbers between 0 and 255"), widget)

        return self.entrys['entry1'].get_text() + "." + self.entrys['entry2'].get_text() + "." +self.entrys['entry3'].get_text() + "." +self.entrys['entry4'].get_text()

    def entry_insert_text_cb(self, entry, text, length, pos, next):
        if text == '.':
            entry.emit_stop_by_name ("insert_text")
            if next:
                next.grab_focus()
            return
        reg = re.compile ("[^0-9]+")
        if reg.match (text):
            entry.emit_stop_by_name ("insert_text")

    get_text = dehydrate
    set_text = hydrate



if __name__ == "__main__":
    def output(xxx, data):
	try:
	    print data.dehydrate()
	except:
	    print "oops errors"
	gtk.main_quit()

    win = gtk.Window()
    win.connect('destroy', gtk.main_quit)
    vbox = gtk.VBox()
    ip = IPEditor()
    vbox = gtk.VBox()
    vbox.pack_start(ip)
    button = gtk.Button("Quit")
    button.connect("pressed", output, ip)
    vbox.pack_start(button, False, False)
    win.add(vbox)
    win.show_all()
    gtk.main()
    
