/* -*- mode: c; c-basic-offset: 4; -*-
 *
 * o2cb_config.h
 *
 * Configuration management routines for the o2cb_ctl utility.
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


#ifndef _O2CB_CONFIG_H
#define _O2CB_CONFIG_H

typedef struct _O2CBConfig	O2CBConfig;
typedef struct _O2CBNode	O2CBNode;

O2CBConfig *o2cb_config_initialize(void);
O2CBConfig *o2cb_config_load(const gchar *filename);
gint o2cb_config_store(O2CBConfig *config, const gchar *filename);
void o2cb_config_free(O2CBConfig *config);

gchar *o2cb_config_get_cluster_name(O2CBConfig *config);
gint o2cb_config_set_cluster_name(O2CBConfig *config,
                                  const gchar *name);

JIterator *o2cb_config_get_nodes(O2CBConfig *config);
O2CBNode *o2cb_config_get_node(O2CBConfig *config, guint n);
O2CBNode *o2cb_config_add_node(O2CBConfig *config);
void o2cb_config_delete_node(O2CBConfig *config, O2CBNode *node);

gint o2cb_node_get_number(O2CBNode *node);
gchar *o2cb_node_get_name(O2CBNode *node);
gchar *o2cb_node_get_ip_string(O2CBNode *node);
gint o2cb_node_get_ipv4(O2CBNode *node, struct in_addr *addr);
guint o2cb_node_get_port(O2CBNode *node);

gint o2cb_node_set_name(O2CBNode *node, const gchar *name);
gint o2cb_node_set_ip_string(O2CBNode *node, const gchar *addr);
gint o2cb_node_set_ipv4(O2CBNode *node, struct in_addr *addr);
void o2cb_node_set_port(O2CBNode *node, guint port);

#endif  /* _O2CB_CONFIG_H */

