/*
 * class_print.c
 *
 * generic print for structures in ocfs file system check utility
 *
 * Copyright (C) 2003 Oracle.  All rights reserved.
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
 * Authors: Kurt Hackel, Sunil Mushran
 */

#include "fsck.h"

/*
 * print_class_member()
 *
 */
int print_class_member(char *buf, ocfs_class *cl, ocfs_class_member *mbr,
		       FILE *out, bool bad)
{
	GString *gs = NULL;
	int ret;
	char *inval_str = "";

	ret = mbr->to_string(&gs, buf, &mbr->type);

	if (ret != 0 || bad)
		inval_str = "<INVALID VALUE> ";
	    
	fprintf(out, "%s: %s%s", mbr->name, inval_str, gs==NULL ? "NULL" : gs->str);
	if (gs)
		g_string_free(gs, true);
	
	return ret;
}				/* print_class_member */

/*
 * _print_class()
 *
 */
int _print_class(char *buf, ocfs_class *cl, FILE *out, bool num, GHashTable *ht)
{
	int ret = 0, i, bad;

	bad = 0;
	fprintf(out, "\n%s\n=================================\n", cl->name);
	for (i=0; i<cl->num_members; i++)
	{
		bool fail;
		ocfs_class_member *mbr = &(cl->members[i]);

		fail = (g_hash_table_lookup(ht, GINT_TO_POINTER(i)) != NULL);
		if (num)
			fprintf(out, "%3d. ", i+1);

		if (print_class_member(buf, cl, mbr, out, fail) != 0)
		{
			bad++;
			ret = -1;
		}
		fprintf(out, "\n");
	}

	if (ret == -1)
		LOG_ERROR("%d bad fields total", bad);

	return ret;
}				/* _print_class */

extern ocfsck_context ctxt;

/*
 * print_class()
 *
 */
int print_class(char *buf, ocfs_class *cl, FILE *out, GHashTable *ht)
{
	if (ctxt.write_changes)
		return _print_class(buf, cl, out, true, ht);
	else
		return _print_class(buf, cl, out, false, ht);
}				/* print_class */
