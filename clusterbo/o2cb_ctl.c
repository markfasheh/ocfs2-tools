/* -*- mode: c; c-basic-offset: 4; -*-
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
#define O2CB_CONFIG_FILE "/etc/cluster.conf"

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
    gchar *oc_manager;
    GList *oc_objects;
    GList *oc_attrs;
    gboolean oc_compact_info;
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
            "Usage: " PROGNAME " -C -l <manager> -n <object> -t <type> [-i] [-a <attribute> ] ...\n"
            "       " PROGNAME " -D -l <manager> -n <object> [-u]\n"
            "       " PROGNAME " -I [-o|-z] -l <manager> [-n <object>] [-t <type>] [-a <attribute>] ...\n"
            "       " PROGNAME " -H -l <manager> [-n <object>] [-t <type>] [-a <attribute>] ...\n"
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
    while ((c = getopt(argc, argv, ":hVCDIHiuozl:n:t:a:-:")) != EOF)
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

            case 'l':
                if (!optarg || !*optarg)
                {
                    fprintf(stderr, PROGNAME ": Argument to \'-l\' cannot be \"\"\n");
                    return -EINVAL;
                }
                ctxt->oc_manager = g_strdup(optarg);
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

    if (!ctxt->oc_manager)
    {
        fprintf(stderr, PROGNAME ": You must specify a manager device\n");
        return -EINVAL;
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
    ctxt->oc_config = o2cb_config_load(O2CB_CONFIG_FILE);
    if (!ctxt->oc_config)
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
    fprintf(stderr,
            PROGNAME ": Discovery by type not yet supported\n");
    return -ENOTSUP;
}  /* find_objects_for_type() */

static gint find_type_for_objects(O2CBContext *ctxt)
{
    int rc;
    gchar *object, *name;
    gulong num;
    JIterator *iter;
    O2CBNode *node;
    
    object = (gchar *)ctxt->oc_objects->data;

    name = o2cb_config_get_cluster_name(ctxt->oc_config);
    if (!strcmp(name, object))
    {
        ctxt->oc_type = O2CB_TYPE_CLUSTER;
        return 0;
    }
    g_free(name);

    num = strtoul(object, &name, 10);
    if (name && !*name && (num < UINT_MAX))
    {
        /* The object key is a number, hence a node number.
         * object == NULL means check num, not object. */
        g_free(object);
        object = NULL;
    }

    iter = o2cb_config_get_nodes(ctxt->oc_config);
    if (!iter)
        return -ENOMEM;

    rc = -ENOENT;
    while (j_iterator_has_more(iter))
    {
        node = (O2CBNode *)j_iterator_get_next(iter);
        if (object)
        {
            name = o2cb_node_get_name(node);
            rc = strcmp(name, object);
            g_free(name);
            if (!rc)
            {
                ctxt->oc_type = O2CB_TYPE_NODE;
                break;
            }
        }
        else
        {
            if (num == o2cb_node_get_number(node))
            {
                ctxt->oc_type = O2CB_TYPE_NODE;
                rc = 0;
                break;
            }
        }
    }

    j_iterator_free(iter);

    return rc;
}  /* find_type_for_objects() */

static gint online_cluster(O2CBContext *ctxt)
{
    errcode_t ret;
    gint rc;
    gchar *name, *node_name;
    JIterator *iter;
    GList *list = NULL, *l;
    O2CBNode *node;
    nm_node_info *node_i;
    struct in_addr addr;

    rc = -ENOMEM;
    name = o2cb_config_get_cluster_name(ctxt->oc_config);
    if (!name)
        goto out_error;

    iter = o2cb_config_get_nodes(ctxt->oc_config);
    if (!iter)
        goto out_error;

    rc = 0;
    while (j_iterator_has_more(iter))
    {
        node = (O2CBNode *)j_iterator_get_next(iter);
        rc = -ENOMEM;
        node_i = g_new0(nm_node_info, 1);
        if (!node_i)
            break;
        list = g_list_append(list, node_i);
        node_i->node_num = o2cb_node_get_number(node);
        node_name = o2cb_node_get_name(node);
        if (!node_name)
            break;
        /* Ignore truncation */
        g_strlcpy(node_i->node_name, node_name, NM_MAX_NAME_LEN);
        g_free(node_name);
        node_i->ifaces[0].ip_port = htons(o2cb_node_get_port(node));
        rc = o2cb_node_get_ipv4(node, &addr);
        if (rc)
            break;
        node_i->ifaces[0].addr_u.ip_addr4 = addr.s_addr;
        rc = 0;
    }
    j_iterator_free(iter);
    if (rc)
        goto out_error;

    rc = -EIO;
    ret = o2cb_set_cluster_name(name);
    if (ret)
    {
        com_err(PROGNAME, ret, "while setting cluster name");
        goto out_error;
    }

    l = list;
    while (l)
    {
        node_i = (nm_node_info *)l->data;
        rc = o2cb_add_node(node_i);
        if (rc)
            goto out_error;
        l = l->next;
    }

    rc = o2cb_activate_cluster();
    if (rc)
        goto out_error;
    rc = o2cb_activate_networking();
    if (rc)
        goto out_error;

out_error:
    g_free(name);

    while (list)
    {
        node_i = (nm_node_info *)list->data;
        list = g_list_delete_link(list, list);

        g_free(node_i);
    }

    return rc;
}  /* online_cluster() */

static gint offline_cluster(O2CBContext *ctxt)
{
    fprintf(stderr,
            PROGNAME ": Offline of cluster not supported yet\n");
    return -ENOTSUP;
}  /* offline_cluster() */

static gint run_change_cluster(O2CBContext *ctxt)
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
        rc = o2cb_config_set_cluster_name(ctxt->oc_config, val);
        if (rc)
            return rc;
    }
    /* FIXME: Should perhaps store config before changing online info */
    if (attr_set(ctxt, "online"))
    {
        if (attr_boolean(ctxt, "online", FALSE))
            rc = online_cluster(ctxt);
        else
            rc = offline_cluster(ctxt);
    }

    return rc;
}  /* run_change_cluster() */

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
        rc = run_change_cluster(ctxt);
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
}  /* run_change() */

gint main(gint argc, gchar *argv[])
{
    int rc;
    O2CBContext ctxt = {0, };

    initialize_o2cb_error_table();
    rc = parse_options(argc, argv, &ctxt);
    if (rc)
        print_usage(rc);

    switch (ctxt.oc_op)
    {
        case O2CB_OP_NONE:
            fprintf(stderr,
                    PROGNAME ": You must specify an operation\n");
            print_usage(-EINVAL);
            break;

        case O2CB_OP_CREATE:
        case O2CB_OP_DELETE:
        case O2CB_OP_INFO:
            rc = -ENOTSUP;
            fprintf(stderr,
                    PROGNAME ": Not yet supported\n");
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

    return 0;
}  /* main() */
