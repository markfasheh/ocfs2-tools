/*
 * main.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
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
 * Authors: Sunil Mushran
 */

#ifndef __MAIN_H__
#define __MAIN_H__

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/raw.h>
#include <linux/kdev_t.h>
#include <inttypes.h>

#include <glib.h>

#include <linux/types.h>

#include "ocfs2.h"
#include "ocfs2_fs.h"
#include "ocfs1_fs_compat.h"

#define FSWRK_FATAL(fmt, arg...)	({ fprintf(stderr, "ERROR at %s, %d: " fmt ".  EXITING!!!\n", \
						   __FILE__, __LINE__, ##arg);  \
					   raise (SIGTERM);	\
					   exit(1); \
					 })

#define FSWRK_COM_FATAL(__p, __r)	do { com_err(__p, __r, "(%s,%d)", __FILE__, __LINE__); raise (SIGTERM); exit(1); } while(0)

#define FSWRK_FATAL_STR(str)		FSWRK_FATAL(str, "")

#define FSWRK_WARN(fmt, arg...)		fprintf(stderr, "WARNING at %s, %d: " fmt ".\n", \
						__FILE__, __LINE__, ##arg)

#define FSWRK_WARN_STR(str)		FSWRK_WARN(str, "")

#undef max
#define max(a,b)	((a) > (b) ? (a) : (b))
#undef min
#define min(a,b)	((a) < (b) ? (a) : (b))

/* remaining headers */
#include <corrupt.h>
#include <chain.h>
#include <extent.h>
#include <fsck_type.h>

#endif		/* __MAIN_H__ */
