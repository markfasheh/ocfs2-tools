bug 3123542
===========

Symptoms
--------
* "ls -l" in a directory produces output like the following:

sh-2.05a$ ls -l /ocfs/include/linux/
ls: /ocfs/include/linux/mroute.h: No such file or directory
ls: /ocfs/include/linux/mount.h: No such file or directory
ls: /ocfs/include/linux/modsetver.h: No such file or directory
ls: /ocfs/include/linux/mm.h: No such file or directory
total 2871
-rwxr-xr-x    1 khackel  khackel      7295 Sep 16 10:42 a.out.h
-rwxr-xr-x    1 khackel  khackel     11089 Sep 16 10:43 ac97_codec.h
-rwxr-xr-x    1 khackel  khackel      2589 Sep 16 10:43 acct.h
-rwxr-xr-x    1 khackel  khackel      1338 Sep 16 10:43 adfs_fs.h
-rwxr-xr-x    1 khackel  khackel       525 Sep 16 10:43 adfs_fs_i.h
....

* lookup (eg. stat) of a file known to exist fails, but readdir() finds
  the file as it should


Cause
-----
In OCFS, unlike most Unix filesystems, filenames are stored as part of
the file (inode) instead of in the parent directory inode.  All of the
file information, including its permissions, name, lock, etc., are
stored in a sector on disk for that file.  The parent directory merely
maintains an index on these files to keep them sorted alphabetically.
These directory blocks can maintain exactly 254 files, so for each
multiple of 254 files within one directory, there will be another
alpha-sorted index.
In some rare cases (eg. a node crash) one of these directory indexes may
be corrupted.  This results in a directory block search that can no
longer find some filenames because searches through these blocks are
done alphabetically.  Functions which still scan all blocks of this
directory will succeed as normal because the index is not needed for a
full search.  Ultimately this means that the "lost" files are actually
still intact on disk and not corrupted.  Only the directory index needs
to be rebuilt.


Solution
--------
bug3123542 executable:

* The bug3123542 executable is a one-off patch which will "mount" the
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
  validity, and can also fix bug 3123542, in addition to several other
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
  fsck.ocfs and bug3123542) on the device to be checked.
* For a consistent view of the filesystem, these commands need to use
  DIRECT-IO to access the device.  With some versions of the linux
  kernel, this can only be done if you first bind a raw device (see
  man raw(8) for more details) to the block device.  Also, in some
  kernel versions, attempting to do DIRECT-IO to a block device will
  fail, and in other versions it may silently succeed but not actually
  give you direct access to the device.  It is safest to always bind a
  raw device to the block device and use this as your target.  Remember
  to free up the raw device when finished.
