/*
 * ocfsipc.h
 *
 * Function prototypes for related 'C' file.
 *
 * Copyright (C) 2002, 2003 Oracle.  All rights reserved.
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

#ifndef _OCFSIPC_H_
#define _OCFSIPC_H_

int ocfs_cleanup_ipc (void);    /* unused and empty */

int ocfs_init_ipc (void);       /* empty */

int ocfs_recv_thread (void *unused);

int ocfs_init_udp (void);

int ocfs_init_ipc_dlm (ocfs_protocol protocol);

int ocfs_send_udp_msg (ocfs_ipc_config_info * send, void *msg, __u32 msglen,
		       wait_queue_head_t * event);

int ocfs_recv_udp_msg (ocfs_recv_ctxt * recv_ctxt);

int ocfs_send_bcast (ocfs_super * osb, __u64 votemap, ocfs_dlm_msg * dlm_msg);

//void ocfs_dlm_send_msg (ocfs_super * osb, ocfs_ipc_config_info * send,
//			ocfs_dlm_msg * dlm_msg);

int ocfs_init_udp_sock (struct socket **send_sock, struct socket **recv_sock);

int ocfs_send_to (struct socket *sock, struct sockaddr *addr,
		  int addrlen, char *buf, int buflen);

int ocfs_recv_from (struct socket *sock, struct sockaddr *addr,
		    int *addrlen, char *buf, int *buflen);

#endif				/* _OCFSIPC_H_ */
