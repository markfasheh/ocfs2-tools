/* -*- mode: c; c-basic-offset: 4; -*-
 *
 * o2cb_config.c
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

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <glib.h>

#include "jiterator.h"
#include "jconfig.h"
#include "o2cb_config.h"


struct _O2CBConfig {
    gchar *c_name;
    guint c_num_nodes;
    GList *c_nodes;
};

struct _O2CBNode {
    guint n_number;
    gchar *n_name;
    gchar *n_addr;
    guint n_port;
};


static void o2cb_node_free(O2CBNode *node);


O2CBConfig *o2cb_config_initialize(void)
{
    O2CBConfig *config;

    config = g_new(O2CBConfig, 1);
    if (!config)
        return NULL;

    config->c_name = NULL;
    config->c_num_nodes = 0;
    config->c_nodes = NULL;

    return config;
}  /* o2cb_config_initialize() */

O2CBConfig *o2cb_config_load(const char *filename)
{
    O2CBConfig *config;

    return config;
}  /* o2cb_config_load() */

static void o2cb_node_free(O2CBNode *node)
{
    if (node->n_name)
        g_free(node->n_name);
    if (node->n_addr)
        g_free(node->n_addr);

    g_free(node);
}  /* o2cb_node_free() */

void o2cb_config_free(O2CBConfig *config)
{
    GList *list;
    O2CBNode *node;

    while (config->c_nodes)
    {
        list = config->c_nodes;
        config->c_nodes = list->next;

        node = (O2CBNode *)list->data;
        g_list_free(list);

        o2cb_node_free(node);
    }

    if (config->c_name)
        g_free(config->c_name);

    g_free(config);
}  /* o2cb_config_free() */

gchar *o2cb_config_get_cluster_name(O2CBConfig *config)
{
    g_return_val_if_fail(config != NULL, NULL);

    return g_strdup(config->c_name);
}  /* o2cb_config_get_cluster_name() */

void o2cb_config_set_cluster_name(O2CBConfig *config)
{
}  /* o2cb_config_set_cluster_name() */

JIterator *o2cb_config_get_nodes(O2CBConfig *config)
{
    g_return_val_if_fail(config != NULL, NULL);

    return j_iterator_new_from_list(config->c_nodes);
}  /* o2cb_config_get_nodes() */

O2CBNode *o2cb_config_get_node(O2CBConfig *config, guint n)
{
    GList *list;
    O2CBNode *node;

    g_return_val_if_fail(config != NULL, NULL);

    list = config->c_nodes;
    while (list)
    {
        node = (O2CBNode *)list->data;
        if (node->n_number == n)
            return node;
        list = list->next;
    }

    return NULL;
}  /* o2cb_config_get_node() */

O2CBNode *o2cb_config_add_node(O2CBConfig *config)
{
    O2CBNode *node;

    g_return_val_if_fail(config != NULL, NULL);

    node = g_new(O2CBNode, 1);

    node->n_name = NULL;
    node->n_addr = NULL;
    node->n_port = 0;
    node->n_number = config->c_num_nodes;
    config->c_num_nodes++;

    config->c_nodes = g_list_append(config->c_nodes, node);

    return node;
}  /* o2cb_config_add_node() */

void o2cb_config_delete_node(O2CBConfig *config, O2CBNode *node)
{
}  /* o2cb_config_delete_node() */

gint o2cb_node_get_number(O2CBNode *node)
{
    g_return_val_if_fail(node != NULL, -1);

    return node->n_number;
}  /* o2cb_node_get_number() */

gchar *o2cb_node_get_name(O2CBNode *node)
{
    g_return_val_if_fail(node != NULL, NULL);

    return g_strdup(node->n_name);
}  /* o2cb_node_get_name() */

gchar *o2cb_node_get_ip_string(O2CBNode *node)
{
    g_return_val_if_fail(node != NULL, NULL);

    return g_strdup(node->n_addr);
}  /* o2cb_node_get_ip_string() */

gint o2cb_node_get_ipv4(O2CBNode *node, struct in_addr *addr)
{
    int rc;

    g_return_val_if_fail(node != NULL, -EINVAL);
    g_return_val_if_fail(node->n_addr != NULL, -ENOENT);

    rc = inet_pton(AF_INET, node->n_addr, addr);
    if (rc < 0)
        return -errno;
    if (!rc)
        return -ENOENT;

    return 0;
}  /* o2cb_node_get_ipv4() */

guint o2cb_node_get_port(O2CBNode *node)
{
    g_return_val_if_fail(node != NULL, 0);

    return node->n_port;
}  /* o2cb_node_get_port() */

gint o2cb_node_set_name(O2CBNode *node, const gchar *name)
{
    gchar *new_name;
    
    new_name = g_strdup(name);
    if (!new_name)
        return -ENOMEM;

    g_free(node->n_name);
    node->n_name = new_name;

    return 0;
}  /* o2cb_node_set_name() */

gint o2cb_node_set_ip_string(O2CBNode *node, const gchar *addr)
{
    gchar *new_addr;
    
    new_addr = g_strdup(addr);
    if (!new_addr)
        return -ENOMEM;

    g_free(node->n_addr);
    node->n_addr = new_addr;

    return 0;
}  /* o2cb_node_set_ip_string() */

gint o2cb_node_set_ipv4(O2CBNode *node, struct in_addr *addr)
{
    gint rc;
    gchar *new_addr;

    g_return_val_if_fail(node != NULL, -EINVAL);

    new_addr = g_malloc0(sizeof(gchar) * INET_ADDRSTRLEN);
    if (!inet_ntop(AF_INET, addr, new_addr, INET_ADDRSTRLEN))
    {
        rc = -errno;
        g_free(new_addr);
        return rc;
    }

    node->n_addr = new_addr;

    return 0;
}  /* o2cb_node_set_ipv4() */

void o2cb_node_set_port(O2CBNode *node, guint port)
{
    g_return_if_fail(node != NULL);

    node->n_port = port;
}  /* o2cb_node_set_port() */

