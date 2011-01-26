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

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <glib.h>

#include "jiterator.h"
#include "jconfig.h"
#include "o2cb_config.h"

#include "o2cb/o2cb.h"


struct _O2CBConfig {
    GList *co_clusters;
    gboolean co_valid;
};

struct _O2CBCluster {
    gchar *c_name;
    gchar *c_hb_mode;
    guint c_num_nodes;
    GList *c_nodes;
    GList *c_heartbeat;
};

struct _O2CBNode {
    guint n_number;
    gchar *n_name;
    gchar *n_addr;
    guint n_port;
};

struct _O2CBHeartbeat {
    gchar *h_region;
};

gchar *valid_heartbeat_modes[] = {
	O2CB_LOCAL_HEARTBEAT_TAG,
	O2CB_GLOBAL_HEARTBEAT_TAG,
	NULL,
};

static void o2cb_node_free(O2CBNode *node);

O2CBConfig *o2cb_config_initialize(void)
{
    O2CBConfig *config;

    config = g_new(O2CBConfig, 1);
    if (!config)
        return NULL;

    config->co_clusters = NULL;
    config->co_valid = FALSE;

    return config;
}  /* o2cb_config_initialize() */

static gint o2cb_cluster_fill_heartbeat(O2CBCluster *cluster,
                                        JConfigStanza *cfs)
{
    O2CBHeartbeat *hb;
    gchar *region;
    gint rc;

    rc = -EINVAL;
    region = j_config_get_attribute(cfs, "region");
    if (!region || !*region)
        goto out_error;

    hb = o2cb_cluster_add_heartbeat(cluster, region);
    if (!hb)
        return -ENOMEM;

    rc = 0;

out_error:
    g_free(region);

    return rc;
}  /* o2cb_config_fill_heartbeat() */

static gint o2cb_cluster_fill_node(O2CBCluster *cluster,
                                   JConfigStanza *cfs)
{
    O2CBNode *node;
    gchar *num_s, *name, *addr, *port_s;
    gchar *ptr;
    gulong val;
    gint rc;

    num_s = addr = port_s = NULL;

    rc = -EINVAL;
    name = j_config_get_attribute(cfs, "name");
    if (!name || !*name)
        goto out_error;

    /* NB: _add_node() gives us a node number, but we're going to
     * override it, because we know better. */
    node = o2cb_cluster_add_node(cluster, name);
    if (!node)
        return -ENOMEM;

    rc = -EINVAL;
    num_s = j_config_get_attribute(cfs, "number");
    if (!num_s || !*num_s)
        goto out_error;
    val = strtoul(num_s, &ptr, 10);
    if (!ptr || *ptr)
        goto out_error;
    rc = -ERANGE;
    if ((val == ULONG_MAX) || (val >= INT_MAX))
        goto out_error;
    o2cb_node_set_number(node, val);

    rc = -EINVAL;
    addr = j_config_get_attribute(cfs, "ip_address");
    if (!addr || !*addr)
        goto out_error;
    rc = o2cb_node_set_ip_string(node, addr);
    if (rc)
        goto out_error;

    rc = -EINVAL;
    port_s = j_config_get_attribute(cfs, "ip_port");
    if (!port_s || !*port_s)
        goto out_error;
    val = strtoul(port_s, &ptr, 10);
    if (!ptr || *ptr)
        goto out_error;
    rc = -ERANGE;
    if ((val == ULONG_MAX) || (val > UINT_MAX))
        goto out_error;
    o2cb_node_set_port(node, val);

    rc = 0;

out_error:
    g_free(num_s);
    g_free(name);
    g_free(addr);
    g_free(port_s);

    return rc;
}  /* o2cb_config_fill_node() */

static gint o2cb_config_fill_cluster(O2CBConfig *config, JConfig *cf,
                                     JConfigStanza *c_cfs)
{
    gint rc;
    gulong val;
    gchar *count, *ptr, *hb_mode = NULL;
    O2CBCluster *cluster;
    JIterator *iter;
    JConfigStanza *n_cfs;
    JConfigMatch match = {J_CONFIG_MATCH_VALUE, "cluster", NULL};

    /* cluster: name */
    rc = -ENOENT;
    match.value = j_config_get_attribute(c_cfs, "name");
    if (!match.value && !*match.value)
        goto out_error;

    cluster = o2cb_config_add_cluster(config, match.value);
    if (!cluster)
        goto out_error;

    /* cluster: heartbeat_mode */
    rc = -ENOMEM;
    hb_mode = j_config_get_attribute(c_cfs, "heartbeat_mode");
    if (!hb_mode)
        hb_mode = g_strdup("local");
    if (!hb_mode)
        goto out_error;
    rc = o2cb_cluster_set_heartbeat_mode(cluster, hb_mode);
    if (rc)
        goto out_error;

    /* node: */
    rc = -ENOMEM;
    iter = j_config_get_stanzas(cf, "node", &match, 1);
    if (!iter)
        goto out_error;

    rc = 0;
    while (j_iterator_has_more(iter))
    {
        n_cfs = (JConfigStanza *)j_iterator_get_next(iter);
        rc = o2cb_cluster_fill_node(cluster, n_cfs);
        if (rc)
            break;
    }
    j_iterator_free(iter);

    if (rc)
        goto out_error;

    /* cluster: node_count */
    rc = -EINVAL;
    count = j_config_get_attribute(c_cfs, "node_count");
    if (!count || !*count)
        goto out_error;
    val = strtoul(count, &ptr, 10);
    if (!ptr || *ptr)
        goto out_error;
    rc = -ERANGE;
    if ((val == ULONG_MAX) || (val > UINT_MAX))
        goto out_error;
    cluster->c_num_nodes = val;

    /* heartbeat: */
    rc = -ENOMEM;
    iter = j_config_get_stanzas(cf, "heartbeat", &match, 1);
    if (!iter)
        goto out_error;

    rc = 0;
    while (j_iterator_has_more(iter))
    {
        n_cfs = (JConfigStanza *)j_iterator_get_next(iter);
        rc = o2cb_cluster_fill_heartbeat(cluster, n_cfs);
        if (rc)
            break;
    }
    j_iterator_free(iter);

    if (rc)
        goto out_error;

    rc = 0;

out_error:
    g_free(hb_mode);
    g_free(match.value);

    return rc;
}  /* o2cb_config_fill_cluster() */

static gint o2cb_config_fill(O2CBConfig *config, JConfig *cf)
{
    int rc;
    JIterator *iter;
    JConfigStanza *c_cfs;

    rc = -ENOMEM;
    iter = j_config_get_stanzas(cf, "cluster", NULL, 0);
    if (!iter)
        goto out_error;

    rc = 0;
    while (j_iterator_has_more(iter))
    {
        c_cfs = (JConfigStanza *)j_iterator_get_next(iter);
        rc = o2cb_config_fill_cluster(config, cf, c_cfs);
        if (rc)
            break;
    }
    j_iterator_free(iter);

out_error:
    return rc;
}

gint o2cb_config_load(const gchar *filename, O2CBConfig **config)
{
    gint rc;
    JConfigCtxt *ctxt;
    JConfig *cf;
    struct stat stat_buf;

    rc = stat(filename, &stat_buf);
    if (rc)
    {
        rc = -errno;
        if (rc != -ENOENT)
            return rc;
        cf = j_config_parse_memory("", strlen(""));
        if (!cf)
            return -ENOMEM;
    }
    else
    {
        ctxt = j_config_new_context();
        if (!ctxt)
            return -ENOMEM;
        j_config_context_set_verbose(ctxt, FALSE);

        cf = j_config_parse_file_with_context(ctxt, filename);
        if (j_config_context_get_error(ctxt))
        {
            if (cf)
            {
                j_config_free(cf);
                cf = NULL;
            }
        }
        j_config_context_free(ctxt);

        if (!cf)
            return -EIO;
    }

    *config = o2cb_config_initialize();
    if (*config)
    {
        rc = o2cb_config_fill(*config, cf);
        if (rc)
        {
            o2cb_config_free(*config);
            *config = NULL;
        }
        else
            (*config)->co_valid = TRUE;
    }

    j_config_free(cf);

    return rc;
}  /* o2cb_config_load() */

static gint o2cb_heartbeat_store(JConfig *cf, O2CBCluster *cluster,
                                 O2CBHeartbeat *hb)
{
    JConfigStanza *cfs;

    cfs = j_config_add_stanza(cf, "heartbeat");
    if (!cfs)
        return -ENOMEM;

    j_config_set_attribute(cfs, "cluster", cluster->c_name);

    j_config_set_attribute(cfs, "region", hb->h_region);

    return 0;
}  /* o2cb_heartbeat_store() */

static gint o2cb_node_store(JConfig *cf, O2CBCluster *cluster,
                            O2CBNode *node)
{
    gchar *val;
    JConfigStanza *cfs;

    cfs = j_config_add_stanza(cf, "node");
    if (!cfs)
        return -ENOMEM;

    j_config_set_attribute(cfs, "cluster", cluster->c_name);

    j_config_set_attribute(cfs, "name", node->n_name);
    j_config_set_attribute(cfs, "ip_address", node->n_addr);

    val = g_strdup_printf("%u", node->n_port);
    if (!val)
        return -ENOMEM;
    j_config_set_attribute(cfs, "ip_port", val);
    g_free(val);

    val = g_strdup_printf("%u", node->n_number);
    if (!val)
        return -ENOMEM;
    j_config_set_attribute(cfs, "number", val);
    g_free(val);

    return 0;
}  /* o2cb_node_store() */

static gint o2cb_cluster_store(JConfig *cf, O2CBCluster *cluster)
{
    int rc;
    gchar *count;
    GList *list;
    JConfigStanza *cfs;
    O2CBNode *node;
    O2CBHeartbeat *hb;

    cfs = j_config_add_stanza(cf, "cluster");

    j_config_set_attribute(cfs, "name", cluster->c_name);
    j_config_set_attribute(cfs, "heartbeat_mode", cluster->c_hb_mode);

    count = g_strdup_printf("%u", cluster->c_num_nodes);
    j_config_set_attribute(cfs, "node_count", count);
    g_free(count);

    rc = 0;
    list = cluster->c_heartbeat;
    while (list)
    {
        hb = (O2CBHeartbeat *)list->data;
        rc = o2cb_heartbeat_store(cf, cluster, hb);
        if (rc)
            break;

        list = list->next;
    }

    list = cluster->c_nodes;
    while (list)
    {
        node = (O2CBNode *)list->data;
        rc = o2cb_node_store(cf, cluster, node);
        if (rc)
            break;

        list = list->next;
    }

    return rc;
}

static gint write_file(const gchar *text, const gchar *filename)
{
    int rc, fd;
    GString *template;
    FILE *file;
    size_t written, len;

    rc = mkdir("/etc/ocfs2", 0755);
    if (rc)
    {
        rc = -errno;
        if (rc == -EEXIST)
            rc = 0;
        else
            goto out;
    }

    rc = -ENOMEM;
    template = g_string_new(filename);
    if (!template)
        goto out;

    g_string_append(template, "XXXXXX");
    fd = mkstemp(template->str);
    rc = -errno;
    if (fd < 0)
        goto out_free;

    file = fdopen(fd, "w");
    if (!file)
    {
        rc = -errno;
        close(fd);
        goto out_unlink;
    }

    len = strlen(text);
    written = fwrite(text, sizeof(char), len, file);
    if (written != len)
    {
        if (feof(file))
            rc = -EIO;
        else if (ferror(file))
            rc = -errno;
        else
            rc = -EIO;

        fclose(file);
        goto out_unlink;
    }

    fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    rc = fclose(file);
    if (rc)
    {
        rc = -errno;
        goto out_unlink;
    }

    rc = rename(template->str, filename);
    if (rc)
        rc = -errno;
    
out_unlink:
    if (rc)
        unlink(template->str);

out_free:
    g_string_free(template, TRUE);

out:
    return rc;
}


gint o2cb_config_store(O2CBConfig *config, const gchar *filename)
{
    int rc;
    char *text;
    JConfig *cf;
    O2CBCluster *cluster;
    GList *list;

    cf = j_config_parse_memory("", strlen(""));
    if (!cf)
        return -ENOMEM;

    rc = 0;
    list = config->co_clusters;
    while (list)
    {
        cluster = (O2CBCluster *)list->data;
        rc = o2cb_cluster_store(cf, cluster);
        if (rc)
            goto out;

        list = list->next;
    }

    rc = -ENOMEM;
    text = j_config_dump_memory(cf);
    if (!text)
        goto out;

    rc = write_file(text, filename);
    g_free(text);

out:
    return rc;
}  /* o2cb_config_store() */

static void o2cb_node_free(O2CBNode *node)
{
    if (node->n_name)
        g_free(node->n_name);
    if (node->n_addr)
        g_free(node->n_addr);

    g_free(node);
}  /* o2cb_node_free() */

static void o2cb_heartbeat_free(O2CBHeartbeat *hb)
{
    if (hb->h_region)
        g_free(hb->h_region);

    g_free(hb);
}  /* o2cb_heartbeat_free() */

static void o2cb_cluster_free(O2CBCluster *cluster)
{
    GList *list;
    O2CBNode *node;
    O2CBHeartbeat *hb;

    while (cluster->c_nodes)
    {
        list = cluster->c_nodes;
        node = (O2CBNode *)list->data;

        cluster->c_nodes = g_list_delete_link(list, list);
        o2cb_node_free(node);
    }

    while (cluster->c_heartbeat)
    {
        list = cluster->c_heartbeat;
        hb = (O2CBHeartbeat *)list->data;

        cluster->c_heartbeat = g_list_delete_link(list, list);
        o2cb_heartbeat_free(hb);
    }

    if (cluster->c_name)
        g_free(cluster->c_name);

    g_free(cluster);
}  /* o2cb_cluster_free() */

static void _o2cb_cluster_free(gpointer data, gpointer user_data)
{
	o2cb_cluster_free((O2CBCluster *)data);
}

void o2cb_config_free(O2CBConfig *config)
{
    g_list_foreach(config->co_clusters, _o2cb_cluster_free, NULL);
    g_list_free(config->co_clusters);
    g_free(config);
}

O2CBCluster *o2cb_config_add_cluster(O2CBConfig *config,
                                     const gchar *name)
{
    O2CBCluster *cluster;

    g_return_val_if_fail(config != NULL, NULL);
    
    cluster = o2cb_config_get_cluster_by_name(config, name);
    if (cluster)
        return NULL;

    cluster = g_new(O2CBCluster, 1);

    cluster->c_name = g_strdup(name);
    cluster->c_hb_mode = g_strdup("local");
    cluster->c_num_nodes = 0;
    cluster->c_nodes = NULL;
    cluster->c_heartbeat = NULL;

    config->co_clusters = g_list_append(config->co_clusters, cluster);

    config->co_valid = TRUE;

    return cluster;
}  /* o2cb_cluster_add_cluster() */

gint o2cb_config_remove_cluster(O2CBConfig *config, const gchar *name)
{
    O2CBCluster *cluster;

    g_return_val_if_fail(config != NULL, -1);
    cluster = o2cb_config_get_cluster_by_name(config, name);
    if (!cluster)
        return -ENOENT;

    config->co_clusters = g_list_remove(config->co_clusters, cluster);
    o2cb_cluster_free(cluster);

    return 0;
}

O2CBCluster *o2cb_config_get_cluster_by_name(O2CBConfig *config,
                                             const gchar *name)
{
    GList *list;
    O2CBCluster *cluster;

    g_return_val_if_fail(config != NULL, NULL);

    list = config->co_clusters;
    while (list)
    {
        cluster = (O2CBCluster *)list->data;
        if (!strcmp(cluster->c_name, name))
            return cluster;
        list = list->next;
    }

    return NULL;
}  /* o2cb_config_get_cluster_by_name() */

JIterator *o2cb_config_get_clusters(O2CBConfig *config)
{
    g_return_val_if_fail(config != NULL, NULL);

    return j_iterator_new_from_list(config->co_clusters);
}  /* o2cb_config_get_clusters() */

gchar *o2cb_cluster_get_name(O2CBCluster *cluster)
{
    g_return_val_if_fail(cluster != NULL, NULL);

    return g_strdup(cluster->c_name);
}  /* o2cb_cluster_get_name() */

gint o2cb_cluster_set_name(O2CBCluster *cluster, const gchar *name)
{
    gchar *new_name;

    new_name = g_strdup(name);
    if (!new_name)
        return -ENOMEM;

    g_free(cluster->c_name);
    cluster->c_name = new_name;

    return 0;
}  /* o2cb_config_set_cluster_name() */

gchar *o2cb_cluster_get_heartbeat_mode(O2CBCluster *cluster)
{
    g_return_val_if_fail(cluster != NULL, NULL);

    return g_strdup(cluster->c_hb_mode);
}  /* o2cb_cluster_get_hb_mode() */

gint o2cb_cluster_set_heartbeat_mode(O2CBCluster *cluster, const gchar *hb_mode)
{
    gchar *new_hb_mode;
    gint i, valid = 0;

    if (cluster->c_hb_mode && !g_ascii_strcasecmp(hb_mode, cluster->c_hb_mode))
        return 0;

    for (i = 0; valid_heartbeat_modes[i]; i++) {
        if (!g_ascii_strcasecmp(hb_mode, valid_heartbeat_modes[i])) {
                valid = 1;
                break;
	}
    }

    if (!valid)
        return -EINVAL;

    new_hb_mode = g_ascii_strdown(hb_mode, -1);
    if (!new_hb_mode)
        return -ENOMEM;

    g_free(cluster->c_hb_mode);
    cluster->c_hb_mode = new_hb_mode;

    return 0;
}  /* o2cb_config_set_cluster_hb_mode() */

JIterator *o2cb_cluster_get_heartbeat_regions(O2CBCluster *cluster)
{
    g_return_val_if_fail(cluster != NULL, NULL);

    return j_iterator_new_from_list(cluster->c_heartbeat);
}  /* o2cb_cluster_get_heartbeat_regions() */

O2CBHeartbeat *o2cb_cluster_get_heartbeat_by_region(O2CBCluster *cluster,
						    const gchar *region)
{
    GList *list;
    O2CBHeartbeat *hb;

    g_return_val_if_fail(cluster != NULL, NULL);

    list = cluster->c_heartbeat;
    while (list)
    {
        hb = (O2CBHeartbeat *)list->data;
        if (!strcmp(hb->h_region, region))
            return hb;
        list = list->next;
    }

    return NULL;
}  /* o2cb_cluster_get_heartbeat_by_region() */

gint o2cb_cluster_remove_heartbeat(O2CBCluster *cluster, const gchar *region)
{
    O2CBHeartbeat *hb;

    g_return_val_if_fail(cluster != NULL, -1);

    hb = o2cb_cluster_get_heartbeat_by_region(cluster, region);
    if (!hb)
        return -1;

    cluster->c_heartbeat = g_list_remove(cluster->c_heartbeat, hb);
    g_free(hb->h_region);
    g_free(hb);

    return 0;
}  /* o2cb_cluster_remove_heartbeat() */

O2CBHeartbeat *o2cb_cluster_add_heartbeat(O2CBCluster *cluster,
                                          const gchar *region)
{
    O2CBHeartbeat *hb;

    g_return_val_if_fail(cluster != NULL, NULL);

    hb = o2cb_cluster_get_heartbeat_by_region(cluster, region);
    if (hb)
        return NULL;

    hb = g_new(O2CBHeartbeat, 1);

    hb->h_region = g_strdup(region);

    cluster->c_heartbeat = g_list_append(cluster->c_heartbeat, hb);

    return hb;
}  /* o2cb_cluster_add_heartbeat() */

gchar *o2cb_heartbeat_get_region(O2CBHeartbeat *heartbeat)
{
    g_return_val_if_fail(heartbeat != NULL, NULL);

    return g_strdup(heartbeat->h_region);
} /* o2cb_heartbeat_get_region */

guint o2cb_cluster_get_node_count(O2CBCluster *cluster)
{
    g_return_val_if_fail(cluster != NULL, 0);

    return cluster->c_num_nodes;
}

JIterator *o2cb_cluster_get_nodes(O2CBCluster *cluster)
{
    g_return_val_if_fail(cluster != NULL, NULL);

    return j_iterator_new_from_list(cluster->c_nodes);
}  /* o2cb_cluster_get_nodes() */

O2CBNode *o2cb_cluster_get_node(O2CBCluster *cluster, guint n)
{
    GList *list;
    O2CBNode *node;

    g_return_val_if_fail(cluster != NULL, NULL);

    list = cluster->c_nodes;
    while (list)
    {
        node = (O2CBNode *)list->data;
        if (node->n_number == n)
            return node;
        list = list->next;
    }

    return NULL;
}  /* o2cb_cluster_get_node() */

O2CBNode *o2cb_cluster_get_node_by_name(O2CBCluster *cluster,
                                        const gchar *name)
{
    GList *list;
    O2CBNode *node;

    g_return_val_if_fail(cluster != NULL, NULL);

    list = cluster->c_nodes;
    while (list)
    {
        node = (O2CBNode *)list->data;
        if (!strcmp(node->n_name, name))
            return node;
        list = list->next;
    }

    return NULL;
}  /* o2cb_cluster_get_node_by_name() */

O2CBNode *o2cb_cluster_add_node(O2CBCluster *cluster,
                                const gchar *name)
{
    O2CBNode *node;

    g_return_val_if_fail(cluster != NULL, NULL);

    node = o2cb_cluster_get_node_by_name(cluster, name);
    if (node)
        return NULL;

    node = g_new(O2CBNode, 1);

    node->n_name = g_strdup(name);
    node->n_addr = NULL;
    node->n_port = 0;
    node->n_number = cluster->c_num_nodes;
    cluster->c_num_nodes++;

    cluster->c_nodes = g_list_append(cluster->c_nodes, node);

    return node;
}  /* o2cb_cluster_add_node() */

gint o2cb_cluster_delete_node(O2CBCluster *cluster, const gchar *name)
{
    O2CBNode *node;
    g_return_val_if_fail(cluster != NULL, -1);
    g_return_val_if_fail(name != NULL, -1);

    node = o2cb_cluster_get_node_by_name(cluster, name);
    if (!node)
        return -ENOENT;

    cluster->c_nodes = g_list_remove(cluster->c_nodes, node);
    cluster->c_num_nodes--;
    g_free(node->n_name);
    g_free(node->n_addr);
    g_free(node);

    return 0;

}  /* o2cb_cluster_delete_node() */

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
    gint rc;
    gchar *new_addr;
    struct in_addr ip;

    /* Let's validate the address, shall we? */
    rc = inet_pton(AF_INET, addr, &ip);
    if (rc < 0)
        return -errno;
    if (!rc)
        return -EINVAL;
    
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

void o2cb_node_set_number(O2CBNode *node, guint num)
{
    g_return_if_fail(node != NULL);

    node->n_number = num;
}  /* o2cb_node_set_number() */
