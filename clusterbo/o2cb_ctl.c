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

#define PROGNAME "o2cb_ctl"

typedef enum {
    O2CB_OP_NONE,
    O2CB_OP_INFO,
    O2CB_OP_CREATE,
    O2CB_OP_DELETE,
    O2CB_OP_CHANGE,
} O2CBOperation;

typedef enum {
    O2CB_TYPE_CLUSTER,
    O2CB_TYPE_NODE,
} O2CBType;

typedef struct _OptionAttr OptionAttr;

struct _OptionAttr
{
    int oa_set;
    gchar *oa_name;
    gchar *oa_value;
};

static void print_version()
{
    fprintf(stdout, PROGNAME " version %s\n", VERSION);
    exit(0);
}  /* print_version() */

static void print_usage(gint rc)
{
    FILE *output = rc ? stderr : stdout;

    fprintf(output,
            "Usage: " PROGNAME " -C -l <manager> -n <object> [-a <attribute> ] ...\n"
           );

    exit(rc);
}  /* print_usage() */

static gint append_attr(const gchar *attr_string, GList **list)
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
    attr->oa_value = g_strdup(p[1] ? p[1] : "");
    attr->oa_set = 1;

    g_strfreev(p);

    *list = g_list_append(*list, attr);
    return 0;
}  /* append_attr() */

extern char *optarg;
extern int optopt;
extern int opterr;
static gint parse_options(gint argc, gchar *argv[], O2CBOperation *op,
                          gchar **manager, gchar **object,
                          gchar **stype, GList **prog_attrs)
{
    int c, rc, doubledash;
    OptionAttr *attr;

    doubledash = 0;
    opterr = 0;
    while ((c = getopt(argc, argv, ":hVCDIHl:n:t:a:-:")) != EOF)
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
                if (!optarg || !*optarg)
                {
                    doubledash = 1;
                    break;
                }

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
                if (*op != O2CB_OP_NONE)
                    return -EINVAL;
                *op = O2CB_OP_CREATE;
                break;

            case 'D':
                if (*op != O2CB_OP_NONE)
                    return -EINVAL;
                *op = O2CB_OP_DELETE;
                break;

            case 'I':
                if (*op != O2CB_OP_NONE)
                    return -EINVAL;
                *op = O2CB_OP_INFO;
                break;

            case 'H':
                if (*op != O2CB_OP_NONE)
                    return -EINVAL;
                *op = O2CB_OP_CHANGE;
                break;

            case 'l':
                if (!optarg || !*optarg)
                {
                    fprintf(stderr, PROGNAME ": Argument to \'-l\' cannot be \"\"\n");
                    return -EINVAL;
                }
                *manager = optarg;
                break;

            case 'n':
                if (!optarg || !*optarg)
                {
                    fprintf(stderr, PROGNAME ": Argument to \'-n\' cannot be \"\"\n");
                    return -EINVAL;
                }
                *object = optarg;
                break;

            case 't':
                if (!optarg || !*optarg)
                {
                    fprintf(stderr, PROGNAME ": Argument to \'-t\' cannot be \"\"\n");
                    return -EINVAL;
                }
                *stype = optarg;
                break;

            case 'a':
                if (!optarg || !*optarg)
                {
                    fprintf(stderr, PROGNAME ": Argument to \'-a\' cannot be \"\"\n");
                    return -EINVAL;
                }
                rc = append_attr(optarg, prog_attrs);
                if (rc)
                    return rc;

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
                return -EINVAL;
                break;
        }

        if (doubledash)
            break;
    }
}  /* parse_options() */


gint main(gint argc, gchar *argv[])
{
    int rc;
    gchar *manager, *target, *stype;
    O2CBOperation op;
    GList *prog_attrs = NULL;

    rc = parse_options(argc, argv, &op,
                       &manager, &target, &stype,
                       &prog_attrs);
    return 0;
}  /* main() */
