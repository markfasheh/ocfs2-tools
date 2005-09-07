/* -*- mode: c; c-basic-offset: 4; -*-
 *  vim:expandtab:shiftwidth=4:tabstop=4:
 *
 * o2cb_hb_ctl.c
 *
 * Control program for O2CB heartbeat.
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
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
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>

#include <glib.h>

#include "jiterator.h"
#include "jconfig.h"

#define PROGNAME "o2cb_hb_ctl"
#define HB_CONFIG_FILE "/etc/ocfs2/heartbeat.conf"


typedef enum {
    HBCTL_OP_NONE,
    HBCTL_OP_START,
    HBCTL_OP_KILL,
    HBCTL_OP_INFO,
} HBCtlOperation;

typedef struct _HBCtlContext HBCtlContext;
typedef struct _HBCtlRegionInfo HBCtlRegionInfo;

struct _HBCtlContext
{
    gchar *c_cluster;
    gchar *c_uuid;
    HBCtlOperation c_op;
    GList *c_regions;
    gint c_all;
    gint c_global;
};

struct _HBCtlRegionInfo
{
    gchar *i_layout;
    gchar *i_uuid;
};


static void print_usage(gint rc);


static gint parse_region(HBCtlContext *ctxt, gchar *line)
{
    gint rc = 0;
    gchar **elements;
    HBCtlRegionInfo *info;

    if (!line || !line[0] || (line[0] == '#'))
        return 0;

    elements = g_strsplit(line, ":", 0);

    if (!elements || !elements[0] || !elements[1] || !elements[2] ||
        elements[3])
    {
        fprintf(stderr,
                PROGNAME ": Invalid heartbeat configuration: \"%s\"\n",
                line);
        rc = -EINVAL;
    }

    info = g_new0(HBCtlRegionInfo, 1);
    info->i_uuid = g_strdup(elements[0]);
    info->i_layout = g_strdup(elements[1]);

    ctxt->c_regions = g_list_append(ctxt->c_regions, info);

    g_strfreev(elements);

    return rc;
}

static gint get_region_info(HBCtlContext *ctxt)
{
    gint rc, argcount, i;
    gint ret;
    GError *error = NULL;
    gchar *output = NULL;
    gchar *errput = NULL;
    gchar **lines;
    gchar *argv[] =
    {
        "o2cb_hb_config",
        "-I",       /* Get info */
        "-o",       /* Parseable output */
        NULL,       /* -c */
        NULL,       /* <cluster> */
        NULL,       /* -u */
        NULL,       /* <uuid */
        NULL
    };

    argcount = 3;  /* Skip to first empty slot */
    if (ctxt->c_cluster)
    {
        argv[argcount++] = "-c";
        argv[argcount++] = ctxt->c_cluster;
    }
    if (ctxt->c_uuid)
    {
        argv[argcount++] = "-u";
        argv[argcount++] = ctxt->c_uuid;
    }

    if (!g_spawn_sync(NULL, argv, NULL,
                      G_SPAWN_SEARCH_PATH |
                      G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                      NULL, NULL,
                      &output, &errput, &ret, &error))
    {
        fprintf(stderr, PROGNAME ": Could not run \"%s\": %s\n",
                argv[0], error->message);
        goto out;
    }

    if (WIFEXITED(ret))
    {
        rc = WEXITSTATUS(ret);
        if (rc)
        {
            fprintf(stderr, PROGNAME ": Error from \"%s\": %s\n",
                    argv[0], errput);
        }
    }
    else if (WIFSIGNALED(ret))
    {
        rc = -EINTR;
        fprintf(stderr,
                PROGNAME ": Program \"%s\" exited with signal %d\n",
                argv[0], WTERMSIG(ret));
    }
    else
    {
        rc = -ENXIO;
        fprintf(stderr,
                PROGNAME ": Program \"%s\" exited unexpectedly\n",
                argv[0]);
    }
    if (rc)
        goto out;

    lines = g_strsplit(output, "\n", 0);
    for (i = 0; lines[i] && !rc; i++)
        rc = parse_region(ctxt, lines[i]);
    g_strfreev(lines);

out:
    return rc;
}

static gint get_mode(HBCtlContext *ctxt)
{
    gint rc, ret;
    GError *error = NULL;
    gchar *output = NULL;
    gchar *errput = NULL;
    gchar *ptr;
    gchar *argv[] =
    {
        "o2cb_hb_config",
        "-M",
        "-o",
        "-c",
        ctxt->c_cluster,
        NULL
    };

    if (!g_spawn_sync(NULL, argv, NULL,
                      G_SPAWN_SEARCH_PATH |
                      G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                      NULL, NULL,
                      &output, &errput, &ret, &error))
    {
        fprintf(stderr, PROGNAME ": Could not run \"%s\": %s\n",
                argv[0], error->message);
        goto out;
    }

    if (WIFEXITED(ret))
    {
        rc = WEXITSTATUS(ret);
        if (rc)
        {
            fprintf(stderr, PROGNAME ": Error from \"%s\": %s\n",
                    argv[0], errput);
        }
    }
    else if (WIFSIGNALED(ret))
    {
        rc = -EINTR;
        fprintf(stderr,
                PROGNAME ": Program \"%s\" exited with signal %d\n",
                argv[0], WTERMSIG(ret));
    }
    else
    {
        rc = -ENXIO;
        fprintf(stderr,
                PROGNAME ": Program \"%s\" exited unexpectedly\n",
                argv[0]);
    }

    if (!rc)
    {
        rc = -ESRCH;
        ptr = strchr(output, '\n');
        ptr++;
        if (strncmp(ptr, ctxt->c_cluster, strlen(ctxt->c_cluster)))
        {
            fprintf(stderr,
                    PROGNAME ": No configuration for cluster \"%s\".\n",
                    ctxt->c_cluster);
            goto out;
        }

        ptr += strlen(ctxt->c_cluster);
        if (*ptr != ':')
        {
            fprintf(stderr,
                    PROGNAME ": Corrupt configuration for cluster \"%s\".\n",
                    ctxt->c_cluster);
            goto out;
        }

        rc = -EINVAL;
        ptr++;
        if (!strcmp(ptr, "local\n"))
            rc = 0;
        else if (!strcmp(ptr, "global\n"))
        {
            rc = 0;
            ctxt->c_global = 1;
        }
        else
        {
            fprintf(stderr,
                    PROGNAME ": Invalid mode for cluster \"%s\": %s",
                    ctxt->c_cluster, ptr);
        }
    }

    g_free(output);
    g_free(errput);

out:
    return rc;
}

static gint do_region_one(const char *layout_driver,
                          const char *op_flag, const char *uuid)
{
    gint rc, ret;
    GError *error = NULL;
    gchar *output = NULL;
    gchar *errput = NULL;
    gchar *argv[] =
    {
        (gchar *)layout_driver,
        (gchar *)op_flag,
        "-u",
        (gchar *)uuid,
        NULL,           /* -q for -I */
        NULL
    };

    if (op_flag[1] == 'I')
        argv[4] = "-q";

    if (!g_spawn_sync(NULL, argv, NULL,
                      G_SPAWN_SEARCH_PATH |
                      G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                      NULL, NULL,
                      &output, &errput, &ret, &error))
    {
        fprintf(stderr, PROGNAME ": Could not run \"%s\": %s\n",
                argv[0], error->message);
        goto out;
    }

    if (WIFEXITED(ret))
    {
        rc = WEXITSTATUS(ret);
        if (rc)
        {
            fprintf(stderr, PROGNAME ": Error from \"%s\": %s\n",
                    argv[0], errput);
        }
    }
    else if (WIFSIGNALED(ret))
    {
        rc = -EINTR;
        fprintf(stderr,
                PROGNAME ": Program \"%s\" exited with signal %d\n",
                argv[0], WTERMSIG(ret));
    }
    else
    {
        rc = -ENXIO;
        fprintf(stderr,
                PROGNAME ": Program \"%s\" exited unexpectedly\n",
                argv[0]);
    }

    if (output && *output)
        fprintf(stdout, "%s", output);

    g_free(output);
    g_free(errput);

out:
    return rc;
}

static gint do_regions(HBCtlContext *ctxt)
{
    gint rc = 0;
    gchar *layout_driver, *op_flag;
    GList *list = ctxt->c_regions;
    HBCtlRegionInfo *info;

    switch (ctxt->c_op)
    {
        case HBCTL_OP_START:
            op_flag = "-S";
            break;

        case HBCTL_OP_KILL:
            op_flag = "-K";
            break;

        case HBCTL_OP_INFO:
            op_flag = "-I";
            break;

        default:
            fprintf(stderr, PROGNAME ": Can't get here!  op %d\n",
                    ctxt->c_op);
            rc = -EINVAL;
            break;
    }

    while (!rc && list)
    {
        info = list->data;
        layout_driver = g_strdup_printf("%s_hb_ctl", info->i_layout);

        rc = do_region_one(layout_driver, op_flag, info->i_uuid);

        g_free(layout_driver);
        list = list->next;
    }

    return rc;
}

static void print_usage(gint rc)
{
    FILE *output = rc ? stderr : stdout;

    fprintf(output,
            "Usage: " PROGNAME " -S -c <cluster> -a\n"
            "       " PROGNAME " -S [-c <cluster> -u <uuid>\n"
            "       " PROGNAME " -K -c <cluster> -a\n"
            "       " PROGNAME " -K [-c <cluster> -u <uuid>\n"
            "       " PROGNAME " -I -c <cluster> -a\n"
            "       " PROGNAME " -I [-c <cluster>] -u <uuid>\n"
            "       " PROGNAME " -h\n"
            "       " PROGNAME " -V\n");

    exit(rc);
}

static void print_version(void)
{
    fprintf(stdout, PROGNAME "version %s\n", VERSION);
    exit(0);
}

extern char *optarg;
extern int optopt;
extern int opterr;
extern int optind;
static gint parse_options(gint argc, gchar *argv[], HBCtlContext *ctxt)
{
    int c;
    HBCtlOperation op = HBCTL_OP_NONE;

    opterr = 0;
    while ((c = getopt(argc, argv, ":hVSKIac:u:-:")) != EOF)
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
                    fprintf(stderr, PROGNAME ": Invalid option \'--%s\'\n",
                            optarg);
                    return -EINVAL;
                }
                break;

            case 'S':
                op = HBCTL_OP_START;
                break;

            case 'K':
                op = HBCTL_OP_KILL;
                break;

            case 'I':
                op = HBCTL_OP_INFO;
                break;

            case 'a':
                ctxt->c_all = 1;
                break;

            case 'c':
                ctxt->c_cluster = optarg;
                break;

            case 'u':
                ctxt->c_uuid = optarg;
                break;

            case '?':
                fprintf(stderr, PROGNAME ": Invalid option: \'-%c'n",
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
                        PROGNAME ": Shouldn't get here %c %c\n",
                        optopt, c);
                return -EINVAL;
                break;
        }

        if (op != HBCTL_OP_NONE)
        {
            if (ctxt->c_op != HBCTL_OP_NONE)
            {
                fprintf(stderr,
                        PROGNAME ": Specify only one operation.\n");
                return -EINVAL;
            }

            ctxt->c_op = op;
            op = HBCTL_OP_NONE;
        }
    }

    if (!ctxt->c_cluster && !ctxt->c_uuid)
    {
        fprintf(stderr, PROGNAME ": Specify a cluster or region UUID to operate on.\n");
        return -EINVAL;
    }

    if (!ctxt->c_all && !ctxt->c_uuid)
    {
        fprintf(stderr,
                PROGNAME ": Specify a region UUID or \'-a\' for all regions in the cluster.\n");

        return -EINVAL;
    }

    if (ctxt->c_uuid && ctxt->c_all)
    {
        fprintf(stderr,
                PROGNAME ": Option \'-a\' is invalid with a region UUID.\n");
        return -EINVAL;
    }

    return 0;
}

gint main(gint argc, gchar *argv[])
{
    gint rc;
    HBCtlContext ctxt = {0, };

    rc = parse_options(argc, argv, &ctxt);
    if (rc)
        print_usage(rc);

    if (ctxt.c_all)
    {
        rc = get_mode(&ctxt);
        if (rc || !ctxt.c_global)
            goto out;
    }

    rc = get_region_info(&ctxt);
    if (rc)
        goto out;

    if (ctxt.c_op == HBCTL_OP_NONE)
    {
        fprintf(stderr, PROGNAME ": Specify an operation.\n");
        print_usage(-EINVAL);
    }

    rc = do_regions(&ctxt);

out:
    return rc;
}
