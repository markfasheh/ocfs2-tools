/*
 * opts.c  Parses options for mount.ocfs2.c
 *
 * Code has been extracted from util-linux-2.12a/mount/mount.c
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 */

#include "mount.ocfs2.h"

#include <ctype.h>
#include <grp.h>
#include <pwd.h>

/* Map from -o and fstab option strings to the flag argument to mount(2).  */
struct opt_map {
  const char *opt;		/* option name */
  int  skip;			/* skip in mtab option string */
  int  inv;			/* true if flag value should be inverted */
  int  mask;			/* flag mask value */
};

/* Custom mount options for our own purposes.  */
/* Maybe these should now be freed for kernel use again */
#define MS_NOAUTO	0x80000000
#define MS_USERS	0x40000000
#define MS_USER		0x20000000
#define MS_OWNER	0x10000000
#define MS_GROUP	0x08000000
#define MS_PAMCONSOLE   0x04000000
#define MS_COMMENT	0x00020000
#define MS_LOOP		0x00010000

/* Options that we keep the mount system call from seeing.  */
#define MS_NOSYS	(MS_NOAUTO|MS_USERS|MS_USER|MS_COMMENT|MS_LOOP|MS_PAMCONSOLE)

/* Options that we keep from appearing in the options field in the mtab.  */
#define MS_NOMTAB	(MS_REMOUNT|MS_NOAUTO|MS_USERS|MS_USER|MS_PAMCONSOLE)

/* Options that we make ordinary users have by default.  */
#define MS_SECURE	(MS_NOEXEC|MS_NOSUID|MS_NODEV)

/* Options that we make owner-mounted devices have by default */
#define MS_OWNERSECURE	(MS_NOSUID|MS_NODEV)

static const struct opt_map opt_map[] = {
  { "defaults",	0, 0, 0		},	/* default options */
  { "ro",	1, 0, MS_RDONLY	},	/* read-only */
  { "rw",	1, 1, MS_RDONLY	},	/* read-write */
  { "exec",	0, 1, MS_NOEXEC	},	/* permit execution of binaries */
  { "noexec",	0, 0, MS_NOEXEC	},	/* don't execute binaries */
  { "suid",	0, 1, MS_NOSUID	},	/* honor suid executables */
  { "nosuid",	0, 0, MS_NOSUID	},	/* don't honor suid executables */
  { "dev",	0, 1, MS_NODEV	},	/* interpret device files  */
  { "nodev",	0, 0, MS_NODEV	},	/* don't interpret devices */
  { "sync",	0, 0, MS_SYNCHRONOUS},	/* synchronous I/O */
  { "async",	0, 1, MS_SYNCHRONOUS},	/* asynchronous I/O */
  { "dirsync",	0, 0, MS_DIRSYNC},	/* synchronous directory modifications */
  { "remount",  0, 0, MS_REMOUNT},      /* Alter flags of mounted FS */
  { "bind",	0, 0, MS_BIND   },	/* Remount part of tree elsewhere */
  { "auto",	0, 1, MS_NOAUTO	},	/* Can be mounted using -a */
  { "noauto",	0, 0, MS_NOAUTO	},	/* Can  only be mounted explicitly */
  { "users",	0, 0, MS_USERS	},	/* Allow ordinary user to mount */
  { "nousers",	0, 1, MS_USERS	},	/* Forbid ordinary user to mount */
  { "user",	0, 0, MS_USER	},	/* Allow ordinary user to mount */
  { "nouser",	0, 1, MS_USER	},	/* Forbid ordinary user to mount */
  { "owner",	0, 0, MS_OWNER  },	/* Let the owner of the device mount */
  { "noowner",	0, 1, MS_OWNER  },	/* Device owner has no special privs */
  { "group",    0, 0, MS_GROUP  },	/* Let the group of the device mount */
  { "nogroup",  0, 1, MS_GROUP  },	/* Device group has no special privs */
  { "_netdev",	0, 0, MS_COMMENT},	/* Device accessible only via network */
  { "comment",  0, 0, MS_COMMENT},	/* fstab comment only (kudzu,_netdev)*/

  /* add new options here */
  { "pamconsole",   0, 0, MS_PAMCONSOLE }, /* Allow users at console to mount */
  { "nopamconsole", 0, 1, MS_PAMCONSOLE }, /* Console user has no special privs */
#ifdef MS_NOSUB
  { "sub",	0, 1, MS_NOSUB	},	/* allow submounts */
  { "nosub",	0, 0, MS_NOSUB	},	/* don't allow submounts */
#endif
#ifdef MS_SILENT
  { "quiet",	0, 0, MS_SILENT    },	/* be quiet  */
  { "loud",	0, 1, MS_SILENT    },	/* print out messages. */
#endif
#ifdef MS_MANDLOCK
  { "mand",	0, 0, MS_MANDLOCK },	/* Allow mandatory locks on this FS */
  { "nomand",	0, 1, MS_MANDLOCK },	/* Forbid mandatory locks on this FS */
#endif
  { "loop",	1, 0, MS_LOOP	},	/* use a loop device */
#ifdef MS_NOATIME
  { "atime",	0, 1, MS_NOATIME },	/* Update access time */
  { "noatime",	0, 0, MS_NOATIME },	/* Do not update access time */
#endif
#ifdef MS_NODIRATIME
  { "diratime",	0, 1, MS_NODIRATIME },	/* Update dir access times */
  { "nodiratime", 0, 0, MS_NODIRATIME },/* Do not update dir access times */
#endif
  { "kudzu",	0, 0, MS_COMMENT },	/* Silently remove this option (backwards compat use only) */
  { "managed",	0, 0, MS_COMMENT },	/* Silently remove this option */
  { NULL,	0, 0, 0	}
};


static char *opt_loopdev, *opt_vfstype, *opt_offset, *opt_encryption,
  *opt_speed, *opt_comment;

static struct string_opt_map {
  char *tag;
  int skip;
  char **valptr;
} string_opt_map[] = {
  { "loop=",	0, &opt_loopdev },
  { "vfs=",	1, &opt_vfstype },
  { "offset=",	0, &opt_offset },
  { "encryption=", 0, &opt_encryption },
  { "speed=", 0, &opt_speed },
  { "comment=", 1, &opt_comment },
  { NULL, 0, NULL }
};

static void clear_string_opts(void)
{
	struct string_opt_map *m;

	for (m = &string_opt_map[0]; m->tag; m++)
		*(m->valptr) = NULL;
}

static int parse_string_opt(char *s)
{
	struct string_opt_map *m;
	int lth;

	for (m = &string_opt_map[0]; m->tag; m++) {
		lth = strlen(m->tag);
		if (!strncmp(s, m->tag, lth)) {
			*(m->valptr) = xstrdup(s + lth);
			return 1;
		}
	}
	return 0;
}

/*
 * Look for OPT in opt_map table and return mask value.
 * If OPT isn't found, tack it onto extra_opts (which is non-NULL).
 * For the options uid= and gid= replace user or group name by its value.
 */
static inline void parse_opt (const char *opt, int *mask, char *extra_opts, int len)
{
	const struct opt_map *om;

	for (om = opt_map; om->opt != NULL; om++)
		if (streq (opt, om->opt)) {
			if (om->inv)
				*mask &= ~om->mask;
			else
				*mask |= om->mask;
			if ((om->mask == MS_USER || om->mask == MS_USERS || om->mask == MS_PAMCONSOLE)
			    && !om->inv)
				*mask |= MS_SECURE;
			if ((om->mask == MS_OWNER || om->mask == MS_GROUP)
			    && !om->inv)
				*mask |= MS_OWNERSECURE;
#ifdef MS_SILENT
			if (om->mask == MS_SILENT && om->inv)  {
				mount_quiet = 1;
				verbose = 0;
			}
#endif
			return;
		}

	len -= strlen(extra_opts);

	if (*extra_opts && --len > 0)
		strcat(extra_opts, ",");

	/* convert nonnumeric ids to numeric */
	if (!strncmp(opt, "uid=", 4) && !isdigit(opt[4])) {
		struct passwd *pw = getpwnam(opt+4);
		char uidbuf[20];

		if (pw) {
			sprintf(uidbuf, "uid=%d", pw->pw_uid);
			if ((len -= strlen(uidbuf)) > 0)
				strcat(extra_opts, uidbuf);
			return;
		}
	}
	if (!strncmp(opt, "gid=", 4) && !isdigit(opt[4])) {
		struct group *gr = getgrnam(opt+4);
		char gidbuf[20];

		if (gr) {
			sprintf(gidbuf, "gid=%d", gr->gr_gid);
			if ((len -= strlen(gidbuf)) > 0)
				strcat(extra_opts, gidbuf);
			return;
		}
	}

	if ((len -= strlen(opt)) > 0)
		strcat(extra_opts, opt);
}
  
/* Take -o options list and compute 4th and 5th args to mount(2).  flags
   gets the standard options (indicated by bits) and extra_opts all the rest */
void parse_opts (char *options, int *flags, char **extra_opts)
{
	*flags = 0;
	*extra_opts = NULL;

	clear_string_opts();

	if (options != NULL) {
		char *opts = xstrdup(options);
		char *opt;
		int len = strlen(opts) + 20;

		*extra_opts = xmalloc(len);
		**extra_opts = '\0';

		for (opt = strtok (opts, ","); opt; opt = strtok (NULL, ","))
			if (!parse_string_opt (opt))
				parse_opt (opt, flags, *extra_opts, len);

		free(opts);
	}

#if 0
	if (readonly)
		*flags |= MS_RDONLY;
	if (readwrite)
		*flags &= ~MS_RDONLY;
	*flags |= mounttype;
#endif
}
