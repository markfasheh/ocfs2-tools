/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * o2cb.h
 *
 * Routines for accessing the o2cb configuration.
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
 */

#ifndef _O2CB_H
#define _O2CB_H

#ifndef _XOPEN_SOURCE
# define _XOPEN_SOURCE 600
#endif
#ifndef _LARGEFILE64_SOURCE
# define _LARGEFILE64_SOURCE
#endif

#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>

#include <linux/types.h>

#include <et/com_err.h>

#if O2CB_FLAT_INCLUDES
#include "o2cb_err.h"

#include "ocfs2_heartbeat.h"
#include "ocfs2_nodemanager.h"
#include "ocfs2_tcp.h"
#else
#include <o2cb/o2cb_err.h>

#include <o2cb/ocfs2_heartbeat.h>
#include <o2cb/ocfs2_nodemanager.h>
#include <o2cb/ocfs2_tcp.h>
#endif

#endif  /* _O2CB_H */
