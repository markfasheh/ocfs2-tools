/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dlmtcp.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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
 * Authors: Kurt Hackel, Mark Fasheh, Sunil Mushran, Wim Coekaerts,
 *	    Manish Singh, Neeraj Goyal, Suchit Kaura
 */

#ifndef DLMNET_H
#define DLMNET_H
#include <linux/socket.h>
#include <sys/socket.h>
#include <linux/inet.h>
#include <linux/in.h>

typedef struct _gsd_ioc
{
	int fd;
	int namelen;
	char name[NM_MAX_NAME_LEN+1];
	int status;
} gsd_ioc;

#define  NET_IOC_MAGIC          'O'
#define  NET_IOC_ACTIVATE       _IOR(NET_IOC_MAGIC, 1, net_ioc)
#define  NET_IOC_GETSTATE       _IOR(NET_IOC_MAGIC, 2, net_ioc)
#define  GSD_IOC_CREATE_GROUP   _IOR(NET_IOC_MAGIC, 3, gsd_ioc)
#define  GSD_IOC_ADD_GROUP_NODE _IOR(NET_IOC_MAGIC, 4, gsd_ioc)

#define GSD_MESSAGE   130
#define GSD_ACTION_ADD_GROUP        (0x01)
#define GSD_ACTION_ADD_GROUP_NODE   (0x02)
typedef struct _gsd_message
{
	u16 from;
	u8 action;
	u8 namelen;
	u8 name[NM_MAX_NAME_LEN];
} gsd_message;

#endif /* DLMNET_H */
