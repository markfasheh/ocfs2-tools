/*
 * parse.c
 *
 * Utility to parse headers for ocfs file system check utility
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

/*  gcc -D_GNU_SOURCE parse.c -o parse . ./parseenv */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <ocfsbool.h>

typedef struct _member
{
	char *name;
	char *type;
	char *flavor;
	char *array_size;
	char *hi;
	char *lo;
	int size;
	bool is_signed;
	int off;
} member;

typedef struct _oclass
{
	char *name;
	int num_members;
	int max_members;
	member *members;
} oclass;

typedef struct _base_type
{
	char *name;
	bool is_signed;
	int size;
} base_type;

base_type known_types[] = 
{ 
	{"u64", false, 8}, 
	{"u32", false, 4},
	{"u16", false, 2},
	{"u8",  false, 1},
	{"s64", true,  8},
	{"s32", true,  4},
	{"s16", true,  2},
	{"s8",  true,  1},
	{"bool",false, 4}
};
int num_known = sizeof(known_types)/sizeof(base_type);

void get_type_and_name(char *str, member *m);
int print_one_member(oclass *cl, member *m, char *prefix);
int print_one_class(oclass *cl);


int num_classes = 0;
int max_classes = 0;
oclass *classes = NULL;

oclass * add_class(oclass *cl)
{
	oclass *ret;

	if (++num_classes > max_classes)
	{
		max_classes += 5;
		classes = realloc(classes, max_classes*sizeof(oclass));
	}
	ret = (oclass *) (((char *)classes) + ((num_classes-1)*sizeof(oclass)));
	memcpy(ret, cl, sizeof(oclass));
	return ret;
}

member * add_member(oclass *cl, member *m)
{
	member *ret;
	if (++(cl->num_members) > cl->max_members)
	{
		cl->max_members += 5;
		cl->members = realloc(cl->members, cl->max_members*sizeof(member));
	}
	ret = (member *) (((char *)cl->members) + ((cl->num_members-1)*sizeof(member)));
	memcpy(ret, m, sizeof(member));
	return ret;
}

int main(int argc, char **argv)
{
	FILE *f;
	char *buf;
	bool in_class;
	int i, j;
	oclass curclass, *cc;

	buf = malloc(4097);
	if (buf==NULL)
		exit(1);

	if (argc < 2)
	{
		f = stdin;
	}
	else
	{
		f = fopen(argv[1], "r");
		if (f == NULL)
			exit(1);
	}

	printf("#include \"fsck.h\"\n\n");

	in_class = false;
	while (1)
	{
		member curmember, *cm = &curmember;
		
		memset(cm, 0, sizeof(member));
		if (fgets(buf, 4097, f) == NULL)
			break;
		if (strstr(buf, "// CLASS"))
		{
			char *tmp, *tmp2, *tmp3;
			in_class = true;
			tmp = strstr(buf, "typedef");
			if ((tmp3 = strstr(tmp, "struct")) == NULL)
				tmp = strstr(tmp, "union");
			else
				tmp = tmp3;
			tmp = strstr(tmp, "_");
			tmp2 = ++tmp;
			while (isalnum(*tmp2) || *tmp2=='_')
				tmp2++;
			*tmp2 = '\0';
			curclass.name = strdup(tmp);
			curclass.num_members = 0;
			curclass.max_members = 0;
			curclass.members = NULL;
			cc = add_class(&curclass);
		}
		else if (strstr(buf, "// END CLASS"))
		{
			in_class = false;
			cc = NULL;
		}
		else if (strstr(buf, "// DISKLOCK"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("disklock");
		}
		else if (strstr(buf, "// IPCONFIG"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("ipconfig");
		}
		else if (strstr(buf, "// GUID"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("guid");
		}
		else if (strstr(buf, "// ATTRIBS"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("attribs");
		}
		else if (strstr(buf, "// BOOL"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("bool");
		}
		else if (strstr(buf, "// CLUSTERSIZE"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("clustersize");
		}
		else if (strstr(buf, "// DATE"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("date");
		}
		else if (strstr(buf, "// DIRFLAG"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("dirflag");
		}
		else if (strstr(buf, "// DIRINDEX"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("dirindex");
		}
		else if (strstr(buf, "// DIRNODEINDEX"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("dirnodeindex");
		}
		else if (strstr(buf, "// DISKPTR"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("diskptr");
		}
		else if (strstr(buf, "// EXTENTTYPE"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("extenttype");
		}
		else if (strstr(buf, "// FILEFLAG"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("fileflag");
		}
		else if (strstr(buf, "// GID"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("gid");
		}
		else if (strstr(buf, "// LOCKLEVEL"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("locklevel");
		}
		else if (strstr(buf, "// NODEBITMAP"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("nodebitmap");
		}
		else if (strstr(buf, "// NODENUM"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("nodenum");
		}
		else if (strstr(buf, "// PERMS"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("perms");
		}
		else if (strstr(buf, "// SYNCFLAG"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("syncflag");
		}
		else if (strstr(buf, "// UID"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("uid");
		}
		else if (strstr(buf, "// UNUSED"))
		{
			// do nothing
		}
		else if (strstr(buf, "// EXTENT["))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("extent_array");
		}
		else if (strstr(buf, "// CHAR"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("char_array");
		}
		else if (strstr(buf, "// HEX"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("hex_array");
		}
		else if (strstr(buf, "// VOTEFLAG"))
		{
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("voteflag_array");
		}
		else if (strstr(buf, "// NUMBER RANGE"))
		{
			char *tmp1,*tmp2;
			tmp1 = strstr(buf, "RANGE");
			tmp1 = strstr(tmp1, "(");
			tmp2 = ++tmp1;
			while (*tmp2!=',')
				tmp2++;
			cm->lo = strndup(tmp1, tmp2-tmp1);
			tmp1 = ++tmp2;
			while (*tmp2!=')')
				tmp2++;
			cm->hi = strndup(tmp1, tmp2-tmp1);
			get_type_and_name(buf, cm);
			cm = add_member(cc, cm);
			cm->flavor = strdup("number_range");
		}
	}

	for (i=0; i<num_classes; i++)
		print_one_class(&(classes[i]));
	
	printf("ocfs_class *ocfs_all_classes[] = {\n");
	
	for (i=0; i<num_classes; i++)
		printf("\t&(%s_class),\n", classes[i].name);

	printf("};\n");
	printf("int ocfs_num_classes = %d;\n", num_classes);

	exit(0);	
}
int print_one_class(oclass *cl)
{
	int i;

	printf("static ocfs_class_member %s_members[] = {\n", cl->name);
	for (i=0; i<cl->num_members; i++)
	{
		member *m = &(cl->members[i]);
		print_one_member(cl, m, NULL);
	}
	printf("};\n");

	printf("ocfs_class %s_class = {\n", cl->name);
	printf("\t\"%s\", \n", cl->name);
	printf("\tsizeof(%s_members) / sizeof(ocfs_class_member), \n", cl->name);
	printf("\t%s_members\n", cl->name);
	printf("};\n");
}

int get_const_array_size(char *name)
{
	int ret;
	char buf[200];
	FILE *pipe;
	
	memset(buf, 0, 200);
	sprintf(buf, "./mkgetconst \"%s\"", name);

	pipe = popen(buf, "r");
	if (pipe==NULL)
		return 0;
	ret = fread(buf, 200, 1, pipe);
	buf[199] = '\0';
	pclose(pipe);
	ret = atoi(buf);

	return ret;
}

int print_one_member(oclass *cl, member *m, char *prefix)
{
	int l, k, j, i;
	base_type *this_type = NULL;

	for (k=0; k<num_known; k++)
	{
		if (strcmp(known_types[k].name, m->type)==0 || 
		    (strlen(m->type)>2 && strcmp(known_types[k].name, &(m->type[2]))==0 &&
			  m->type[0]=='_' && m->type[1]=='_' ))
		{
			this_type = &(known_types[k]);
			break;
		}
	}
	
	if (this_type == NULL)
	{
		if (prefix == NULL)
		{
			// TODO: get each of the subtype fields and print them here	
			for (i=0; i<num_classes; i++)
			{
				oclass *subcl;
				subcl = &(classes[i]);
				if (strcmp(subcl->name, m->type)==0)
				{
					if (m->array_size)
					{
						char arrname[50];
						int asz;
					        asz = atoi(m->array_size);
						if (asz==0)
						{
							asz = get_const_array_size(m->array_size);
							// char *env_sz = getenv(m->array_size);
							if (asz == 0)
								exit(1);
						}

						for (l=0; l<asz; l++)
						{
							sprintf(arrname, "%s[%d]", m->name, l);
							for (j=0; j<subcl->num_members; j++)
								print_one_member(cl, &(subcl->members[j]), arrname);
						}
					}
					else
					{
						for (j=0; j<subcl->num_members; j++)
							print_one_member(cl, &(subcl->members[j]), m->name);
					}
					return 0;
				}
			}
		}
		// either 2 levels deep or failed to find class
		exit(1);
	}
	
	// open member
	printf("\t{\n");
	
	// name
	if (prefix)
		printf("\t\t\"%s.%s\", ", prefix, m->name);
	else
		printf("\t\t\"%s\", ", m->name);
		
	// flavor
	printf("\"%s\",\n", m->flavor);
	
	// typeinfo: { off, array_size, type_size, is_signed, lo, hi }
	if (prefix)
		printf("\t\t{ G_STRUCT_OFFSET(%s, %s.%s), ", cl->name, prefix, m->name);
	else
		printf("\t\t{ G_STRUCT_OFFSET(%s, %s), ", cl->name, m->name);
	printf("%s, ", m->array_size ? m->array_size : "0");
	printf("%d, ", this_type->size);
	printf("%s, ", this_type->is_signed ? "true" : "false");
	if (m->lo && m->hi)
		printf("%s, %s },\n", m->lo, m->hi);
	else
		printf("0, 0 },\n");

	// valid
	printf("\t\t_%s_valid, ", m->flavor);

	// to_string
	printf("_%s_to_string_%s, ",  m->flavor, this_type->name);

	// from_string
	printf("_string_to_%s_%s, ",  m->flavor, this_type->name);

	// helptext
	printf("_get_%s_helptext", m->flavor);

	// close member	
	printf("\n\t},\n");
	return 0;
}

void get_type_and_name(char *str, member *m)
{
	char *tmp, *tmp2, *tmp3;

	tmp = str;
	while (*tmp != ';')
		tmp++;
	*tmp = '\0';
	tmp--;
	while (isalnum(*tmp) || *tmp=='[' || *tmp==']' || *tmp=='_' || *tmp=='+')
		tmp--;
	tmp++;
	if ((tmp2=strchr(tmp, '[')) != NULL)
	{
		tmp3 = ++tmp2;
		while (*tmp3 != ']')
			tmp3++;
		*tmp3 = '\0';
		m->array_size = strdup(tmp2);
		*(--tmp2) = '\0';
	}
	m->name = strdup(tmp);
	tmp--;
	tmp2 = str;
	while (isspace(*tmp2))
		tmp2++;
	while (isspace(*tmp))
		tmp--;
	*(++tmp) = '\0';
	m->type = strdup(tmp2);
}
