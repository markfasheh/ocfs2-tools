bug 3016598
===========

Symptoms
--------
* "rmdir foo" of an empty directory "foo" in OCFS fails (or even rm -rf foo)
* debugocfs output for the undeletable directory shows that num_ent_used
  is still greater than 0  
  (ex. debugocfs -d /path/to/dir/ /dev/device)
* if the "old" file entries immediately following this directory block
  are inspected with debugocfs, the sync_flags field will be set to
  OCFS_SYNC_FLAG_MARK_FOR_DELETION on all remaining files in the
  directory, and thus they are not visible 
  (ex. debugocfs -X -h ## -l ## -t ocfs_file_entry /dev/device, where 
   the -h and -l params are the high and low 64-bit offsets to the block in
   question.  to find the first block in a directory, add 512 to the 
   node_disk_off field from the "debugocfs -d" above.  each subsequent
   file entry will be 512 bytes after the previous)


Cause
-----
At certain rare times in OCFS, a file will be marked as deleted but
its parent directory will never decrement the num_ent_used count of
existing files in that directory.  However, the deleted file will be
marked as such, and therefore skipped when a file listing (ls) is done
in that directory.  The directory will appear empty but the user will be
unable to delete it.  This is different from a directory which cannot be
deleted because it is in use by a process on the node attempting the
delete or another node in the cluster.  Use standard tools (lsof, fuser)
to determine whether the directory is simply in use, or whether you
actually have this corruption in the filesystem.


Solution
--------
bug3016598 executable:

* The bug3016598 executable is a one-off patch which will "mount" the
  filesystem, take locks on necessary filesystem structures, make a fix
  to any affected indices, unlock and unmount.  While it is safe to run
  this utility (if directed to do so by support), you should attempt to
  minimize activity on the affected partition and/or directory while
  running.
* Use this utility if you are concerned about downtime and cannot afford
  to bring down the entire cluster to clean up the filesystem.  If the
  filesystem is already dismounted on all nodes, please see the
  fsck.ocfs option below.
* DO NOT run more that one instance of this utility on this node or
  other nodes in the cluster.
* DO NOT run any other one-off patch utilities on this node or any other 
  nodes in the cluster at the same time.
* DO NOT run fsck.ocfs at the same time as this utility.  (you can and
  should run fsck.ocfs.ro (readonly) before and after running this patch
  to verify that the problem is fixed and that no other filesystem
  corruptions have taken place)
* This utility requires an autoconfig slot (it defaults to the last one,
  number 31) and will not be able to run if all slots are filled.  Other
  nodes will see the utility as a full-fledged node.


fsck.ocfs (included as part of OCFS tools):

* The fsck.ocfs command is a full filesystem checker for OCFS.  It does
  *not* do any filesystem locking and therefore can only be run when the
  volume in question is dismounted on all nodes.  
* This utility checks many structures within the filesystem for
  validity, and can also fix bug 3016598, in addition to several other
  bugs.
* A heartbeat check will be performed before running to attempt to
  prevent the user from making changes while any nodes have this
  filesystem mounted.  DO NOT circumvent this by mounting on another
  node immediately after the heartbeat check.  Wait until fsck.ocfs
  completes before mounting.


fsck.ocfs.ro (included as part of OCFS tools):

* This is a readonly version of fsck.ocfs that can be run at any time to
  detect any of the problems normally found by fsck.ocfs.  Redirect the
  output of this command to a file to assist support in determining any
  problems with your filesystem.
* Can be safely run at any time, but may temporarily do a great deal of
  I/O against the device.  
* Sometimes reports false positives for problems.  Filesystem structures
  are in-flight during some system calls.  Re-run the utility few more
  times to see if the errors reported are still present.


Note about all utilities:

* These utilities require at least read access (and write access for
  fsck.ocfs and bug3016598) on the device to be checked.
* For a consistent view of the filesystem, these commands need to use
  DIRECT-IO to access the device.  With some versions of the linux
  kernel, this can only be done if you first bind a raw device (see
  man raw(8) for more details) to the block device.  Also, in some
  kernel versions, attempting to do DIRECT-IO to a block device will
  fail, and in other versions it may silently succeed but not actually
  give you direct access to the device.  It is safest to always bind a
  raw device to the block device and use this as your target.  Remember
  to free up the raw device when finished.
