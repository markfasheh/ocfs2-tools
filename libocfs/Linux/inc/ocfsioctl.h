/*
 * ocfsioctl.h
 *
 * Function prototypes for related 'C' file.
 *
 * Copyright (C) 2002 Oracle Corporation.  All rights reserved.
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
 * Authors: Neeraj Goyal, Suchit Kaura, Kurt Hackel, Sunil Mushran,
 *          Manish Singh, Wim Coekaerts
 */

#ifndef _OCFSIOCTL_H_
#define _OCFSIOCTL_H_

#define  OCFS_NAME              "OCFS"

int ocfs_ioctl (struct inode *inode,
	    struct file *filp, unsigned int cmd, unsigned long arg);

/* structure read by ioctl cmd */
typedef struct _ocfs_ioc
{
	char name[255];		/* "OCFS" */
	char version[255];	/* version */
	__u16 nodenum;		/* node number */
	char nodename[255];	/* node name */
} ocfs_ioc;

/* ioctl commands */
#define  OCFS_IOC_MAGIC          'O'
#define  OCFS_IOC_GETTYPE        _IOR(OCFS_IOC_MAGIC, 1, ocfs_ioc)

/* OCFS_CDSL defined in ocfsvol.h */
#define  OCFS_IOC_CDSL_MODIFY    _IOR(OCFS_IOC_MAGIC, 2, ocfs_cdsl)
#define  OCFS_IOC_CDSL_GETINFO   _IOR(OCFS_IOC_MAGIC, 3, ocfs_cdsl)

#endif				/* _OCFSIOCTL_H_ */
