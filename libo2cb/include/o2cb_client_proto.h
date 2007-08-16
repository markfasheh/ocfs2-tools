/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 */

/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**  
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 *  This copyrighted material is made available to anyone wishing to use,
 *  modify, copy, or redistribute it subject to the terms and conditions
 *  of the GNU General Public License v.2.
 */

#ifndef __O2CB_CLIENT_PROTO_H
#define __O2CB_CLIENT_PROTO_H

#ifdef HAVE_CMAN

/* Basic communication properties */
#define OCFS2_CONTROLD_MAXLINE		256
#define OCFS2_CONTROLD_MAXARGS		16
#define OCFS2_CONTROLD_SOCK_PATH	"ocfs2_controld_sock"
#define OCFS2_FSTYPE "ocfs2"

/* Client messages */
typedef enum {
	CM_MOUNT,
	CM_MRESULT,
	CM_UNMOUNT,
	CM_STATUS,
} client_message;

int client_listen(void);
int client_connect(void);
const char *message_to_string(client_message message);
int send_message(int fd, client_message message, ...);
int receive_message(int fd, char *buf, client_message *message,
		    char **argv);
int receive_message_full(int fd, char *buf, client_message *message,
			 char **argv, char **rest);

#endif  /* HAVE_CMAN */

#endif  /* __O2CB_CLIENT_PROTO_H */
