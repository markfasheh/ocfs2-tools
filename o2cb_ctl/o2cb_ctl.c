/* -*- mode: c; c-basic-offset: 4; -*-
 *  vim:expandtab:shiftwidth=4:tabstop=4:
 *
 *
 * o2cb_ctl.c
 *
 * Control program for O2CB.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>

#include <glib.h>

#include "jiterator.h"
#include "o2cb_config.h"

#include "o2cb.h"

#define PROGNAME "o2cb_ctl"
#define O2CB_CONFIG_FILE "/etc/ocfs2/cluster.conf"

typedef enum {
    O2CB_OP_NONE = 0,
    O2CB_OP_INFO,
    O2CB_OP_CREATE,
    O2CB_OP_DELETE,
    O2CB_OP_CHANGE,
} O2CBOperation;

typedef enum {
    O2CB_TYPE_NONE = 0,
    O2CB_TYPE_CLUSTER,
    O2CB_TYPE_NODE,
} O2CBType;

typedef struct _O2CBContext O2CBContext;
typedef struct _OptionAttr OptionAttr;

struct _O2CBContext
{
    O2CBOperation oc_op;
    O2CBType oc_type;
    GList *oc_objects;
    GList *oc_attrs;
    gboolean oc_compact_info;
    gboolean oc_changed;
    gboolean oc_modify_running;
    O2CBConfig *oc_config;
};

struct _OptionAttr
{
    int oa_set;
    gchar *oa_name;
    gchar *oa_value;
};


static void print_version(void)
{
    fprintf(stdout, PROGNAME " version %s\n", VERSION);
    exit(0);
}  /* print_version() */

static void print_usage(gint rc)
{
    FILE *output = rc ? stderr : stdout;

    fprintf(output,
            "Usage: " PROGNAME " -C -n <object> -t <type> [-i] [-a <attribute> ] ...\n"
            "       " PROGNAME " -D -n <object> [-u]\n"
            "       " PROGNAME " -I [-o|-z] [-n <object>] [-t <type>] [-a <attribute>] ...\n"
            "       " PROGNAME " -H [-n <object>] [-t <type>] [-a <attribute>] ...\n"
            "       " PROGNAME " -h\n"
            "       " PROGNAME " -V\n");

    exit(rc);
}  /* print_usage() */

static gint add_object(O2CBContext *ctxt, const gchar *object)
{
    ctxt->oc_objects = g_list_append(ctxt->oc_objects,
                                     g_strdup(object));

    return 0;
}

static gboolean valid_attr(O2CBContext *ctxt, OptionAttr *attr)
{
    int i;
    struct va_table
    {
        O2CBType va_type;
        gchar *va_name;
    } vat[] = { 
        {O2CB_TYPE_CLUSTER, "name"},
        {O2CB_TYPE_CLUSTER, "online"},
        {O2CB_TYPE_NODE, "name"},
        {O2CB_TYPE_NODE, "cluster"},
        {O2CB_TYPE_NODE, "number"},
        {O2CB_TYPE_NODE, "ip_address"},
        {O2CB_TYPE_NODE, "ip_port"},
    };

    for (i = 0; i < (sizeof(vat) / sizeof(*vat)); i++)
    {
        if ((vat[i].va_type == ctxt->oc_type) &&
            !strcmp(vat[i].va_name, attr->oa_name))
                return TRUE;
    }

    return FALSE;
}  /* valid_attr() */

/* Must be called after oc_type is set */
static gint validate_attrs(O2CBContext *ctxt)
{
    GList *list;
    OptionAttr *attr;

    list = ctxt->oc_attrs;

    while (list)
    {
        attr = (OptionAttr *)(list->data);
        if (!valid_attr(ctxt, attr))
        {
            fprintf(stderr,
                    PROGNAME ": Invalid attribute: \"%s\"\n",
                    attr->oa_name);
            return -EINVAL;
        }
        list = list->next;
    }

    return 0;
}  /* validate_attrs() */

static void clear_attrs(O2CBContext *ctxt)
{
    GList *list;
    OptionAttr *attr;

    list = ctxt->oc_attrs;

    if (!list)
        return;

    while (list)
    {
        attr = (OptionAttr *)list->data;
        if (attr->oa_name)
            g_free(attr->oa_name);
        if (attr->oa_value)
            g_free(attr->oa_value);
        g_free(attr);

        list->data = NULL;
        list = list->next;
    }

    g_list_free(ctxt->oc_attrs);
}  /* clear_attrs() */


static gboolean attr_set(O2CBContext *ctxt, const gchar *attr_name)
{
    OptionAttr *attr;
    GList *list;

    list = ctxt->oc_attrs;
    while (list)
    {
        attr = (OptionAttr *)list->data;
        if (!strcmp(attr->oa_name, attr_name))
            return attr->oa_set;
        list = list->next;
    }

    return FALSE;
}  /* attr_set() */


static const gchar *attr_string(O2CBContext *ctxt,
                                const gchar *attr_name,
                                const gchar *def_value)
{
    OptionAttr *attr;
    GList *list;

    list = ctxt->oc_attrs;
    while (list)
    {
        attr = (OptionAttr *)list->data;
        if (!strcmp(attr->oa_name, attr_name))
        {
            if (!attr->oa_set)
                return def_value;
            return attr->oa_value;
        }

        list = list->next;
    }

    return def_value;
}  /* attr_string() */
  

static gboolean attr_boolean(O2CBContext *ctxt,
                             const gchar *attr_name,
                             gboolean def_value)
{
    int i;
    GList *list;
    OptionAttr *attr;
    struct b_table
    {
        gchar *match;
        gboolean value;
    } bt[] = {
        {"0", FALSE}, {"1", TRUE},
        {"f", FALSE}, {"t", TRUE},
        {"false", FALSE}, {"true", TRUE},
        {"n", FALSE}, {"y", TRUE},
        {"no", FALSE}, {"yes", TRUE},
        {"off", FALSE}, {"on", TRUE}
    };

    list = ctxt->oc_attrs;
    while (list)
    {
        attr = (OptionAttr *)list->data;
        if (!strcmp(attr->oa_name, attr_name))
        {
            if (!attr->oa_set || !attr->oa_value || !*(attr->oa_value))
                break;
            for (i = 0; i < (sizeof(bt) / sizeof(*bt)); i++)
            {
                if (!strcmp(bt[i].match, attr->oa_value))
                    return bt[i].value;
            }
            fprintf(stderr,
                    PROGNAME ": Invalid value for attribute \"%s\": %s\n",
                    attr_name, attr->oa_value);
            return -EINVAL;
        }

        list = list->next;
    }

    return def_value;
}  /* attr_boolean() */

static gint append_attr(O2CBContext *ctxt, const gchar *attr_string)
{
    char **p;
    OptionAttr *attr;

    p = g_strsplit(attr_string, "=", 2);
    if (!p)
        return -ENOMEM;

    if (!p[0] || !*p[0])
        return -EINVAL;

    attr = g_new(OptionAttr, 1);
    attr->oa_name = g_strdup(p[0]);
    attr->oa_value = g_strdup(p[1]);
    attr->oa_set = 1;

    g_strfreev(p);

    ctxt->oc_attrs = g_list_append(ctxt->oc_attrs, attr);
    return 0;
}  /* append_attr() */

extern char *optarg;
extern int optopt;
extern int opterr;
extern int optind;
static gint parse_options(gint argc, gchar *argv[], O2CBContext *ctxt)
{
    int c, rc;
    gboolean mi, mu, mo, mz;

    mi = mu = mo = mz = FALSE;
    opterr = 0;
    while ((c = getopt(argc, argv, ":hVCDIHiuozn:t:a:-:")) != EOF)
    {
        switch (c)
        {
            case 'h':
                print_usage(0);
                break;

            case 'V':
                print_version();
                break;
                
            case '-':
                if (!strcmp(optarg, "version"))
                    print_version();
                else if (!strcmp(optarg, "help"))
                    print_usage(0);
                else
                {
                    fprintf(stderr, PROGNAME ": Invalid option: \'--%s\'\n",
                            optarg);
                    return -EINVAL;
                }
                break;

            case 'C':
                if (ctxt->oc_op != O2CB_OP_NONE)
                    return -EINVAL;
                ctxt->oc_op = O2CB_OP_CREATE;
                break;

            case 'D':
                if (ctxt->oc_op != O2CB_OP_NONE)
                    return -EINVAL;
                ctxt->oc_op = O2CB_OP_DELETE;
                break;

            case 'I':
                if (ctxt->oc_op != O2CB_OP_NONE)
                    return -EINVAL;
                ctxt->oc_op = O2CB_OP_INFO;
                break;

            case 'H':
                if (ctxt->oc_op != O2CB_OP_NONE)
                    return -EINVAL;
                ctxt->oc_op = O2CB_OP_CHANGE;
                break;

            case 'i':
                mi = TRUE;
                break;

            case 'u':
                mu = TRUE;
                break;

            case 'z':
                mz = TRUE;
                break;

            case 'o':
                mo = TRUE;
                break;

            case 'n':
                if (!optarg || !*optarg)
                {
                    fprintf(stderr, PROGNAME ": Argument to \'-n\' cannot be \"\"\n");
                    return -EINVAL;
                }
                add_object(ctxt, optarg);
                break;

            case 't':
                if (!optarg || !*optarg)
                {
                    fprintf(stderr, PROGNAME ": Argument to \'-t\' cannot be \"\"\n");
                    return -EINVAL;
                }
                if (!strcmp(optarg, "cluster"))
                    ctxt->oc_type = O2CB_TYPE_CLUSTER;
                else if (!strcmp(optarg, "node"))
                    ctxt->oc_type = O2CB_TYPE_NODE;
                else
                {
                    fprintf(stderr, PROGNAME ": Object type \"%s\" is invalid\n", optarg);
                    return -EINVAL;
                }
                break;

            case 'a':
                if (!optarg || !*optarg)
                {
                    fprintf(stderr, PROGNAME ": Argument to \'-a\' cannot be \"\"\n");
                    return -EINVAL;
                }
                rc = append_attr(ctxt, optarg);
                if (rc)
                    return rc;
                break;

            case '?':
                fprintf(stderr, PROGNAME ": Invalid option: \'-%c\'\n",
                        optopt);
                return -EINVAL;
                break;

            case ':':
                fprintf(stderr,
                        PROGNAME ": Option \'-%c\' requires an argument\n",
                        optopt);
                return -EINVAL;
                break;

            default:
                fprintf(stderr,
                        PROGNAME ":Shouldn't get here %c %c\n",
                        optopt, c);
                return -EINVAL;
                break;
        }
    }

    if (optind < argc) {
        fprintf(stderr, PROGNAME ": Extraneous arguments: \"");
        for (; optind < argc; optind++)
        {
            fprintf(stderr, "%s", argv[optind]);
            if ((optind + 1) < argc)
                fprintf(stderr, " ");
        }
        fprintf(stderr, "\"\n");
        return -E2BIG;
    }

    if (mu && (ctxt->oc_op != O2CB_OP_DELETE))
        c = 'u';
    else if (mi && (ctxt->oc_op != O2CB_OP_CREATE))
        c = 'i';
    else if (mz && (ctxt->oc_op != O2CB_OP_INFO))
        c = 'z';
    else if (mo && (ctxt->oc_op != O2CB_OP_INFO))
        c = 'o';

    if (c != EOF)
    {
        fprintf(stderr, PROGNAME ": Argument \'-%c\' is not valid for this operation\n", c);
        return -EINVAL;
    }

    if (mz && mo)
    {
        fprintf(stderr, PROGNAME ": Cannot specify \'-z\' and \'-o\' at the same time\n");
        return -EINVAL;
    }

    if ((ctxt->oc_op == O2CB_OP_INFO) && mo)
        ctxt->oc_compact_info = TRUE;
    if ((ctxt->oc_op == O2CB_OP_CREATE) && mi)
        ctxt->oc_modify_running = TRUE;
    if ((ctxt->oc_op == O2CB_OP_DELETE) && mu)
        ctxt->oc_modify_running = TRUE;

    return 0;
}  /* parse_options() */

static gint load_config(O2CBContext *ctxt)
{
    gint rc;

    rc = o2cb_config_load(O2CB_CONFIG_FILE, &ctxt->oc_config);
    if (rc)
    {
        fprintf(stderr,
                PROGNAME ": Unable to load cluster configuration file \"%s\"\n",
                O2CB_CONFIG_FILE);
        return -EIO;
    }

    return 0;
}  /* load_config() */

static gint write_config(O2CBContext *ctxt)
{
    gint rc;

    g_return_val_if_fail(ctxt->oc_config != NULL, -EINVAL);

    rc = o2cb_config_store(ctxt->oc_config, O2CB_CONFIG_FILE);
    if (rc)
    {
        fprintf(stderr,
                PROGNAME ": Unable to store cluster configuration file \"%s\": %s\n",
                O2CB_CONFIG_FILE, g_strerror(-rc));
    }

    return rc;
}  /* write_config() */

static gint find_objects_for_type(O2CBContext *ctxt)
{
    int rc = 0;
    JIterator *c_iter, *n_iter;
    O2CBCluster *cluster;
    O2CBNode *node;

    c_iter = o2cb_config_get_clusters(ctxt->oc_config);

    if (!c_iter)
    {
        rc = -ENOMEM;
        goto out;
    }

    while (j_iterator_has_more(c_iter))
    {
        cluster = j_iterator_get_next(c_iter);

        if (ctxt->oc_type == O2CB_TYPE_CLUSTER)
        {
            add_object(ctxt, o2cb_cluster_get_name(cluster));
        }
        else if (ctxt->oc_type == O2CB_TYPE_NODE)
        {
            n_iter = o2cb_cluster_get_nodes(cluster);
            if (!n_iter)
            {
                rc = -ENOMEM;
                break;
            }

            while (j_iterator_has_more(n_iter))
            {
                node = j_iterator_get_next(n_iter);
                add_object(ctxt, o2cb_node_get_name(node));
            }
            j_iterator_free(n_iter);
        }
        else
            abort();
    }
    j_iterator_free(c_iter);

out:
    return rc;
}  /* find_objects_for_type() */

static gint find_type_for_objects(O2CBContext *ctxt)
{
    int rc;
    gchar *object, *name;
    gulong num;
    JIterator *c_iter;
    O2CBNode *node;
    O2CBCluster *cluster;
    
    object = (gchar *)ctxt->oc_objects->data;

    cluster = o2cb_config_get_cluster_by_name(ctxt->oc_config,
                                              object);
    if (cluster)
    {
        rc = 0;
        ctxt->oc_type = O2CB_TYPE_CLUSTER;
        goto out;
    }

    num = strtoul(object, &name, 10);
    if (!name || *name || (num == UINT_MAX))
        num = UINT_MAX;
    
    rc = -ENOMEM;
    c_iter = o2cb_config_get_clusters(ctxt->oc_config);
    if (!c_iter)
        goto out;

    rc = -ENOENT;
    while (j_iterator_has_more(c_iter))
    {
        cluster = (O2CBCluster *)j_iterator_get_next(c_iter);

        node = o2cb_cluster_get_node_by_name(cluster, object);
        if (!node && (num < UINT_MAX))
            node = o2cb_cluster_get_node(cluster, num);

        if (node)
        {
            ctxt->oc_type = O2CB_TYPE_NODE;
            rc = 0;
            break;
        }
    }
    j_iterator_free(c_iter);
    
out:
    return rc;
}  /* find_type_for_objects() */

static gint run_info_clusters(O2CBContext *ctxt)
{
    GList *list;
    O2CBCluster *cluster;
    gchar *name;
    gchar *format;


    if (ctxt->oc_compact_info)
    {
        format = "%s:%u:%s\n";
        fprintf(stdout, "#name:count:status\n");
    }
    else
    {
        format = "cluster:\n"
            "\tname = %s\n"
            "\tnode_count = %u\n"
            "\tstatus = %s\n"
            "\n";
    }

    list = ctxt->oc_objects;
    while (list)
    {
        name = list->data;
        cluster = o2cb_config_get_cluster_by_name(ctxt->oc_config,
                                                  name);
        if (!cluster)
        {
            fprintf(stderr, "Cluster \"%s\" does not exist\n",
                    name);
            return -ENOENT;
        }

        fprintf(stdout, format, o2cb_cluster_get_name(cluster),
                o2cb_cluster_get_node_count(cluster),
                "configured");

        list = list->next;
    }

    return 0;
}

static gint run_info_nodes(O2CBContext *ctxt)
{
    GList *list;
    JIterator *iter;
    O2CBCluster *cluster;
    O2CBNode *node;
    gchar *name;
    gchar *format;


    if (ctxt->oc_compact_info)
    {
        format = "%s:%s:%u:%s:%d:%s\n";
        fprintf(stdout, "#name:cluster:number:ip_address:ip_port:status\n");
    }
    else
    {
        format = "node:\n"
            "\tname = %s\n"
            "\tcluster = %s\n"
            "\tnumber = %u\n"
            "\tip_address = %s\n"
            "\tip_port = %d\n"
            "\tstatus = %s\n"
            "\n";
    }

    list = ctxt->oc_objects;
    while (list)
    {
        name = list->data;

        iter = o2cb_config_get_clusters(ctxt->oc_config);
        if (!iter)
            return -ENOMEM;

        cluster = NULL; /* For gcc */
        node = NULL;
        while (j_iterator_has_more(iter))
        {
            cluster = j_iterator_get_next(iter);

            node = o2cb_cluster_get_node_by_name(cluster, name);
            if (node)
                break;
        }
        j_iterator_free(iter);

        if (!node)
        {
            fprintf(stderr, "Node \"%s\" does not exist\n", name);
            return -ENOENT;
        }

        fprintf(stdout, format,
                o2cb_node_get_name(node),
                o2cb_cluster_get_name(cluster),
                o2cb_node_get_number(node),
                o2cb_node_get_ip_string(node),
                o2cb_node_get_port(node),
                "configured");

        list = list->next;
    }

    return 0;
}

static gint run_info(O2CBContext *ctxt)
{
    gint rc;

    if (!ctxt->oc_type && !ctxt->oc_objects)
    {
        fprintf(stderr,
                PROGNAME ": Operation \'-I\' requires an object or object type\n");
        return -EINVAL;
    } 

    rc = validate_attrs(ctxt);
    if (rc)
        return rc;

    rc = load_config(ctxt);
    if (rc)
        return rc;

    if (ctxt->oc_type && !ctxt->oc_objects)
    {
        rc = find_objects_for_type(ctxt);
        if (rc)
            goto out_error;
    }
    else if (ctxt->oc_objects && !ctxt->oc_type)
    {
        rc = find_type_for_objects(ctxt);
        if (rc)
            goto out_error;
    }

    if (ctxt->oc_type == O2CB_TYPE_NODE)
    {
        rc = run_info_nodes(ctxt);
        if (rc)
            goto out_error;
    }
    else if (ctxt->oc_type == O2CB_TYPE_CLUSTER)
    {
        rc = run_info_clusters(ctxt);
        if (rc)
            goto out_error;
    }
    else
    {
        rc = -EINVAL;
        fprintf(stderr,
                PROGNAME ": Invalid object type!\n");
        goto out_error;
    }

out_error:
    return rc;
}  /* run_info() */

static errcode_t o2cb_node_is_local(gchar *node_name, gboolean *is_local)
{
    char hostname[PATH_MAX];
    size_t host_len, node_len = strlen(node_name);
    gboolean local = 0;
    errcode_t ret = 0;

    ret = gethostname(hostname, sizeof(hostname));
    if (ret) {
        fprintf(stderr, "gethostname() failed: %s", strerror(errno));
        ret = O2CB_ET_HOSTNAME_UNKNOWN;
        goto out;
    }

    host_len = strlen(hostname);
    if (host_len < node_len)
        goto out;

    /* nodes are only considered local if they match the hostname.  we want
     * to be sure to catch the node name being "localhost" and the hostname
     * being "localhost.localdomain".  we consider them equal if the 
     * configured node name matches the start of the hostname up to a '.' */
    if (!strncasecmp(node_name, hostname, node_len) &&
        (hostname[node_len] == '\0' || hostname[node_len] == '.'))
            local = 1;
out:
    *is_local = local;

    return ret;
}

static gint online_cluster(O2CBContext *ctxt, O2CBCluster *cluster)
{
    errcode_t ret;
    gint rc;
    gchar *name, *node_name, *node_num, *ip_address, *ip_port, *local;
    JIterator *iter;
    O2CBNode *node;
    gboolean seen_local = 0, is_local;

    rc = -ENOMEM;
    name = o2cb_cluster_get_name(cluster);
    if (!name)
        goto out_error;

    rc = -EIO;
    ret = o2cb_create_cluster(name);
    if (ret)
    {
        if (ret != O2CB_ET_CLUSTER_EXISTS)
        {
            com_err(PROGNAME, ret, "while setting cluster name");
            goto out_error;
        }
    }
    else
        fprintf(stdout, "Cluster %s created\n", name);

    rc = -ENOMEM;
    iter = o2cb_cluster_get_nodes(cluster);
    if (!iter)
        goto out_error;

    rc = 0;
    while (j_iterator_has_more(iter))
    {
        node = (O2CBNode *)j_iterator_get_next(iter);
        node_num = g_strdup_printf("%d", o2cb_node_get_number(node));
        node_name = o2cb_node_get_name(node);
        ip_port = g_strdup_printf("%d", o2cb_node_get_port(node));
        ip_address = o2cb_node_get_ip_string(node);
            
        ret = o2cb_node_is_local(node_name, &is_local);
        if (ret) {
                com_err(PROGNAME, ret, "while determining if node %s is local",
                        node_name);
                rc = -EINVAL;
                goto out_error;
        }

        if (is_local) {
            if (seen_local) {
                ret = O2CB_ET_CONFIGURATION_ERROR;
                com_err(PROGNAME, ret, "while adding node %s.  It is "
                        "considered local but another node was already marked "
                        "as local.  Do multiple node names in the config "
                        "match this machine's host name?", node_name);
                rc = -EINVAL;
                goto out_error;
            }
            local = g_strdup("1");
            seen_local = 1;
        } else
            local = g_strdup("0");

        ret = o2cb_add_node(name, node_name, node_num, ip_address,
                            ip_port, local);
        if (ret)
        {
            if (ret != O2CB_ET_NODE_EXISTS)
            {
                com_err(PROGNAME, ret, "while adding node %s\n",
                        node_name);
                rc = -EIO;
            }

            ret = 0;
        }
        else
            fprintf(stdout, "Node %s added\n", node_name);

        g_free(node_num);
        g_free(node_name);
        g_free(ip_port);
        g_free(ip_address);
        g_free(local);
        if (rc)
            break;
    }
    j_iterator_free(iter);
    if (rc)
        goto out_error;

    if (!seen_local) {
        ret = O2CB_ET_CONFIGURATION_ERROR;
        com_err(PROGNAME, ret, "while populating cluster %s.  None of its "
                "nodes were considered local.  A node is considered local "
                "when its node name in the configuration matches this "
                "machine's host name.", name);
        rc = -EINVAL;
        goto out_error;
    }

out_error:

    return rc;
}  /* online_cluster() */

static gint offline_cluster(O2CBContext *ctxt, O2CBCluster *cluster)
{
    errcode_t ret;
    gint rc;
    gchar *cluster_name = NULL;
    char **node_name = NULL;
    int i = 0;

    rc = -ENOMEM;
    cluster_name = o2cb_cluster_get_name(cluster);
    if (!cluster_name)
        goto out_error;

    ret = o2cb_list_nodes(cluster_name, &node_name);
    if (ret && ret != O2CB_ET_SERVICE_UNAVAILABLE) {
        com_err(PROGNAME, ret, "while listing nodes in cluster '%s'",
                cluster_name);
        goto out_error;
    }

    rc = -EIO;
    while(node_name && node_name[i] && *(node_name[i])) {
        ret = o2cb_del_node(cluster_name, node_name[i]);
        if (ret) {
            com_err(PROGNAME, ret, "while deleting node '%s' in cluster '%s'",
                    node_name[i], cluster_name);
            goto out_error;
        }
        i++;
    }

    ret = o2cb_remove_cluster(cluster_name);
    if (ret && ret != O2CB_ET_SERVICE_UNAVAILABLE) {
        com_err(PROGNAME, ret, "while removing cluster '%s'", cluster_name);
        goto out_error;
    }

    rc = 0;

out_error:
    if (node_name)
        o2cb_free_nodes_list(node_name);

    if (cluster_name)
        g_free(cluster_name);

    return rc;
}  /* offline_cluster() */

static gint run_change_cluster_one(O2CBContext *ctxt,
                                   O2CBCluster *cluster)
{
    gint rc = 0;
    const gchar *val;

    if (attr_set(ctxt, "name"))
    {
        if (ctxt->oc_modify_running)
        {
            fprintf(stderr,
                    PROGNAME ": Cannot change name of a running cluster\n");
            return -EINVAL;
        }
        val = attr_string(ctxt, "name", NULL);
        if (!val || !*val)
        {
            fprintf(stderr,
                    PROGNAME ": Empty name for cluster\n");
            return -EINVAL;
        }
        rc = o2cb_cluster_set_name(cluster, val);
        if (rc)
            return rc;
        ctxt->oc_changed = TRUE;
    }
    /* FIXME: Should perhaps store config before changing online info */
    if (attr_set(ctxt, "online"))
    {
        if (attr_boolean(ctxt, "online", FALSE))
            rc = online_cluster(ctxt, cluster);
        else
            rc = offline_cluster(ctxt, cluster);
    }

    return rc;
}  /* run_change_cluster_one() */

static gint run_change_clusters(O2CBContext *ctxt)
{
    gint rc = 0;
    O2CBCluster *cluster;
    GList *list;

    list = ctxt->oc_objects;
    while (list)
    {
        cluster = o2cb_config_get_cluster_by_name(ctxt->oc_config,
                                                  (gchar *)list->data);
        if (!cluster)
        {
            rc = -ENOENT;
            fprintf(stderr,
                    PROGNAME ": Cluster \"%s\" does not exist\n",
                    (gchar *)list->data);
            break;
        }

        rc = run_change_cluster_one(ctxt, cluster);
        if (rc)
            break;

        list = list->next;
    }

    return rc;
}

static gint run_change(O2CBContext *ctxt)
{
    gint rc;

    if (!ctxt->oc_type && !ctxt->oc_objects)
    {
        fprintf(stderr,
                PROGNAME ": Operation \'-H\' requires an object or object type\n");
        return -EINVAL;
    } 

    rc = validate_attrs(ctxt);
    if (rc)
        return rc;

    rc = load_config(ctxt);
    if (rc)
        return rc;

    if (ctxt->oc_type && !ctxt->oc_objects)
    {
        rc = find_objects_for_type(ctxt);
        if (rc)
            goto out_error;
    }
    else if (ctxt->oc_objects && !ctxt->oc_type)
    {
        rc = find_type_for_objects(ctxt);
        if (rc)
            goto out_error;
    }

    if (ctxt->oc_type == O2CB_TYPE_NODE)
    {
        rc = -ENOTSUP;
        fprintf(stderr,
                PROGNAME ": Node changes not yet supported\n");
        goto out_error;
    }
    else if (ctxt->oc_type == O2CB_TYPE_CLUSTER)
    {
        rc = run_change_clusters(ctxt);
        if (rc)
            goto out_error;
    }
    else
    {
        rc = -EINVAL;
        fprintf(stderr,
                PROGNAME ": Invalid object type!\n");
        goto out_error;
    }

    if (ctxt->oc_changed)
        rc = write_config(ctxt);

out_error:
    return rc;
}  /* run_change() */

static gint run_create_clusters(O2CBContext *ctxt)
{
    gint rc = 0;
    O2CBCluster *cluster;
    errcode_t err;
    gchar *name;
    GList *list;

    list = ctxt->oc_objects;
    while (list)
    {
        name = list->data;
        cluster = o2cb_config_get_cluster_by_name(ctxt->oc_config,
                                                  name);
        if (cluster)
        {
            rc = -EEXIST;
            fprintf(stderr,
                    PROGNAME ": Cluster \"%s\" already exists\n",
                    name);
            break;
        }

        cluster = o2cb_config_add_cluster(ctxt->oc_config, name);
        if (!cluster)
        {
            rc = -ENOMEM;
            fprintf(stderr,
                    PROGNAME ": Unable to add cluster \"%s\"\n",
                    name);
            break;
        }

        if (ctxt->oc_modify_running)
        {
            err = o2cb_create_cluster(name);
            if (err)
            {
                if (err != O2CB_ET_CLUSTER_EXISTS)
                {
                    rc = -EIO;
                    com_err(PROGNAME, err, "while creating cluster");
                    break;
                }
            }
            else
                fprintf(stdout, "Cluster %s created\n", name);
        }

        list = list->next;
    }

    return rc;
}

static gint run_create_nodes(O2CBContext *ctxt)
{
    gint rc;
    O2CBCluster *cluster;
    O2CBNode *node, *tmpnode;
    errcode_t err;
    long num;
    gchar *ptr;
    gchar *name, *number, *local;
    const gchar *cluster_name, *ip_address, *ip_port;
    gboolean is_local;

    rc = -EINVAL;

    if (ctxt->oc_objects->next)
    {
        fprintf(stderr,
                PROGNAME ": Cannot create more than one node at a time\n");
        goto out;
    }

    cluster_name = attr_string(ctxt, "cluster", NULL);
    if (!cluster_name || !*cluster_name)
    {
        fprintf(stderr,
                PROGNAME ": \"cluster\" attribute required to create a node\n");
        goto out;
    }

    ip_address = attr_string(ctxt, "ip_address", NULL);
    if (!ip_address || !*ip_address)
    {
        fprintf(stderr,
                PROGNAME ": \"ip_address\" attribute required to create a node\n");
        goto out;
    }

    ip_port = attr_string(ctxt, "ip_port", NULL);
    if (!ip_port || !*ip_port)
    {
        fprintf(stderr,
                PROGNAME ": \"ip_port\" attribute required to create a node\n");
        goto out;
    }
    
    rc = 0;

    name = ctxt->oc_objects->data;

    cluster = o2cb_config_get_cluster_by_name(ctxt->oc_config,
                                              cluster_name);
    if (!cluster)
    {
        rc = -ENOENT;
        fprintf(stderr,
                PROGNAME ": Cluster \"%s\" does not exist\n",
                cluster_name);
        goto out;
    }

    node = o2cb_cluster_get_node_by_name(cluster, name);
    if (node)
    {
        rc = -EEXIST;
        fprintf(stderr,
                PROGNAME ": Node \"%s\" already exists\n",
                name);
        goto out;
    }

    node = o2cb_cluster_add_node(cluster, name);
    if (!node)
    {
        rc = -ENOMEM;
        fprintf(stderr,
                PROGNAME ": Unable to add node \"%s\"\n",
                name);
        goto out;
    }

    rc = o2cb_node_set_ip_string(node, ip_address);
    if (rc)
    {
        fprintf(stderr,
                PROGNAME ": IP address \"%s\" is invalid\n",
                ip_address);
        goto out;
    }

    num = strtol(ip_port, &ptr, 10);
    if (!ptr || *ptr || (num < 0) ||
        (num > (guint16)-1))
    {
        rc = -ERANGE;
        fprintf(stderr,
                PROGNAME ": Port number \"%s\" is invalid\n",
                ip_port);
        goto out;
    }

    o2cb_node_set_port(node, num);

    number = g_strdup(attr_string(ctxt, "number", NULL));
    if (number)
    {
        num = strtol(number, &ptr, 10);
        if (!ptr || *ptr || (num < 0) ||
            (num > INT_MAX))
        {
            rc = -ERANGE;
            fprintf(stderr,
                    PROGNAME ": Node number \"%s\" is invalid\n",
                    number);
            g_free(number);
            goto out;
        }

        tmpnode = o2cb_cluster_get_node(cluster, num);
        if (tmpnode && (tmpnode != node))
        {
            rc = -EEXIST;
            fprintf(stderr,
                    PROGNAME ": Node number \"%ld\" already exists\n",
                    num);
            g_free(number);
            goto out;
        }
        o2cb_node_set_number(node, num);
    }
    else
        number = g_strdup_printf("%d", o2cb_node_get_number(node));

    if (ctxt->oc_modify_running)
    {
        err = o2cb_node_is_local(name, &is_local);
        if (err) {
            com_err(PROGNAME, err, "while determining if node %s is local",
                    name);
            rc = -EINVAL;
            goto out;
        }

        if (is_local)
            local = g_strdup("1");
        else
            local = g_strdup("0");
        
        err = o2cb_add_node(cluster_name, name, number,
                            ip_address, ip_port, local);
        if (err)
        {
            if (err != O2CB_ET_NODE_EXISTS)
            {
                rc = -EIO;
                com_err(PROGNAME, err, "while creating node");
            }
        }
        else
            fprintf(stdout, "Node %s created\n", name);
        g_free(local);
    }

    g_free(number);

out:
    return rc;
}  /* run_create_nodes() */

static gint run_create(O2CBContext *ctxt)
{
    gint rc;

    if (!ctxt->oc_type || !ctxt->oc_objects)
    {
        fprintf(stderr,
                PROGNAME ": Operation \'-C\' requires an object and an object type\n");
        return -EINVAL;
    } 

    rc = validate_attrs(ctxt);
    if (rc)
        return rc;

    rc = load_config(ctxt);
    if (rc)
        return rc;

    if (ctxt->oc_type == O2CB_TYPE_NODE)
    {
        rc = run_create_nodes(ctxt);
        if (rc)
            goto out_error;
    }
    else if (ctxt->oc_type == O2CB_TYPE_CLUSTER)
    {
        rc = run_create_clusters(ctxt);
        if (rc)
            goto out_error;
    }
    else
    {
        rc = -EINVAL;
        fprintf(stderr,
                PROGNAME ": Invalid object type!\n");
        goto out_error;
    }

    rc = write_config(ctxt);

out_error:
    return rc;
}  /* run_create() */

gint main(gint argc, gchar *argv[])
{
    int rc;
    errcode_t ret;
    O2CBContext ctxt = {0, };

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    initialize_o2cb_error_table();

    rc = parse_options(argc, argv, &ctxt);
    if (rc)
        print_usage(rc);

    ret = o2cb_init();
    if (ret) {
	    com_err(PROGNAME, ret, "Cannot initialize cluster\n");
	    rc = -EINVAL;
	    goto out_error;
    }

    switch (ctxt.oc_op)
    {
        case O2CB_OP_NONE:
            fprintf(stderr,
                    PROGNAME ": You must specify an operation\n");
            print_usage(-EINVAL);
            break;

        case O2CB_OP_CREATE:
            rc = run_create(&ctxt);
            break;

        case O2CB_OP_DELETE:
            rc = -ENOTSUP;
            fprintf(stderr,
                    PROGNAME ": Not yet supported\n");
            break;

        case O2CB_OP_INFO:
            rc = run_info(&ctxt);
            break;

        case O2CB_OP_CHANGE:
            rc = run_change(&ctxt);
            break;

        default:
            rc = -EINVAL;
            fprintf(stderr,
                    PROGNAME ": Eeek!  Invalid operation!\n");
            break;
    }

    if (ctxt.oc_config)
        o2cb_config_free(ctxt.oc_config);
    clear_attrs(&ctxt);

out_error:
    return rc;
}  /* main() */
