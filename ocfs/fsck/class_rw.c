/*
 * class_rw.c
 *
 * reader-writer functions for each type
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

/*****************************************************
   Implement a type reader-writer function for each type

   Create some kind of type array[] for each class or
   change the one i already have

   Develop an opaque iterator object that deals with running
   thru each thing in this type array for editing class

   Process:

   * Verify the whole structure at once: this should
     probably return the number of bad fields, should
     probably pass the opaque structure, maybe the
     opaque can hold each of the suggested values for
     each bad entry?

   * Iterate w/the opaque thru the print() function,
     showing something for each messed up field

   * Should the edit() iterate thru all of them?, or
     know to get at one by index# ??
 
*****************************************************/

#include "fsck.h"
#include "classes.h"  

// for now, ultraslow-nonhashed-stinkyperformance
// eh, who cares
ocfs_class_member * find_class_member(ocfs_class *cl, const char *name, int *idx)
{
	int i;
	ocfs_class_member *ret;

	*idx = -1;
	for (i=0; i < cl->num_members; i++)
	{
		ret = &(cl->members[i]);
		if (strcmp(ret->name, name)==0)
		{
			*idx = i;
			return ret;
		}
	}
	return NULL;
}
	
	
// ATTRIBS: __u32
int _attribs_valid(void *top, typeinfo *info)
{
	__u32 attribs = G_STRUCT_MEMBER(__u32, top, info->off);
	if (!(attribs & (OCFS_ATTRIB_DIRECTORY| OCFS_ATTRIB_FILE_CDSL| OCFS_ATTRIB_CHAR|
			 OCFS_ATTRIB_BLOCK| OCFS_ATTRIB_REG| OCFS_ATTRIB_FIFO|
			 OCFS_ATTRIB_SYMLINK| OCFS_ATTRIB_SOCKET)) &&
	    attribs != 0)
	{
		return -1;
	}
	return 0;
}

int _attribs_to_string_u32(GString **retval, void *top, typeinfo *info)
{
	__u32 attribs;
	
	attribs = G_STRUCT_MEMBER(__u32, top, info->off);
	if (_attribs_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", attribs);
#endif
		return -1;
	}

	*retval = g_string_new("");
	if (attribs & OCFS_ATTRIB_FILE_CDSL)
		g_string_append(*retval, "cdsl ");

	switch (attribs & ~OCFS_ATTRIB_FILE_CDSL)
	{
		case OCFS_ATTRIB_DIRECTORY:
			g_string_append(*retval, "directory");
			break;
		case OCFS_ATTRIB_CHAR:
			g_string_append(*retval, "character device");
			break;
		case OCFS_ATTRIB_BLOCK:
			g_string_append(*retval, "block device");
			break;
		case OCFS_ATTRIB_FIFO:
			g_string_append(*retval, "fifo");
			break;
		case OCFS_ATTRIB_SYMLINK:
			g_string_append(*retval, "symlink");
			break;
		case OCFS_ATTRIB_SOCKET:
			g_string_append(*retval, "socket");
			break;
		default:
		case OCFS_ATTRIB_REG:
			g_string_append(*retval, "regular file");
			break;
	}
	
	return 0;
}

int _string_to_attribs_u32(char *newval, void *top, typeinfo *info)
{
	__u32 attribs = 0;

	if (strcasecmp(newval, "dir")==0)
		attribs |= OCFS_ATTRIB_DIRECTORY;
	else if (strcasecmp(newval, "char")==0)
		attribs |= OCFS_ATTRIB_CHAR;
	else if (strcasecmp(newval, "block")==0)
		attribs |= OCFS_ATTRIB_BLOCK;
	else if (strcasecmp(newval, "reg")==0)
		attribs |= OCFS_ATTRIB_REG;
	else if (strcasecmp(newval, "fifo")==0)
		attribs |= OCFS_ATTRIB_FIFO;
	else if (strcasecmp(newval, "symlink")==0)
		attribs |= OCFS_ATTRIB_SYMLINK;
	else if (strcasecmp(newval, "socket")==0)
		attribs |= OCFS_ATTRIB_SOCKET;
	else if (strcasecmp(newval, "cdsl-dir")==0)
		attribs |= (OCFS_ATTRIB_DIRECTORY|OCFS_ATTRIB_FILE_CDSL);
	else if (strcasecmp(newval, "cdsl-reg")==0)
		attribs |= (OCFS_ATTRIB_REG|OCFS_ATTRIB_FILE_CDSL);
	else
		return -1;

	G_STRUCT_MEMBER(__u32, top, info->off) = attribs;
	return 0;
}

char * _get_attribs_helptext(typeinfo *info)
{
	return strdup("one of: dir char block reg fifo symlink socket cdsl-dir cdsl-reg");
}


// BOOL: __s32 __u8 bool
int _bool_valid(void *top, typeinfo *info)
{
	__s32 b = G_STRUCT_MEMBER(__s32, top, info->off);
	if (b != 0 && b != 1)
		return -1;
	return 0;
}

int _bool_to_string_s32(GString **retval, void *top, typeinfo *info)
{
	__s32 val = 0;

	val = G_STRUCT_MEMBER(__s32, top, info->off);
	if (_bool_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%d", val);
#endif
		return -1;
	}
	*retval = g_string_new("");
	g_string_append(*retval, val ? "TRUE" : "FALSE");

	return 0;
}

int _string_to_bool_s32(char *newval, void *top, typeinfo *info)
{
	__s32 val;

	if (strcasecmp(newval, "true")==0)
		val = 1;
	else if (strcasecmp(newval, "false")==0)
		val = 0;
	else
		return -1;

	G_STRUCT_MEMBER(__s32, top, info->off) = val;

	return 0;
}

int _bool_to_string_u8(GString **retval, void *top, typeinfo *info)
{
	__u8 val = 0;

	val = G_STRUCT_MEMBER(__u8, top, info->off);
	if (_bool_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", val);
#endif
		return -1;
	}
	*retval = g_string_new("");
	g_string_append(*retval, val ? "TRUE" : "FALSE");

	return 0;
}

int _string_to_bool_u8(char *newval, void *top, typeinfo *info)
{
	__u8 val;

	if (strcasecmp(newval, "true")==0)
		val = 1;
	else if (strcasecmp(newval, "false")==0)
		val = 0;
	else
		return -1;

	G_STRUCT_MEMBER(__u8, top, info->off) = val;

	return 0;
}

int _bool_to_string_bool(GString **retval, void *top, typeinfo *info)
{
	return _bool_to_string_s32(retval, top, info);
}

int _string_to_bool_bool(char *newval, void *top, typeinfo *info)
{
	return _string_to_bool_s32(newval, top, info);
}

char * _get_bool_helptext(typeinfo *info)
{
	return strdup("TRUE or FALSE");
}


// CLUSTERSIZE: __u64 
int _clustersize_valid(void *top, typeinfo *info)
{
	__u64 cs = G_STRUCT_MEMBER(__u64, top, info->off);
	if (cs==4096 || cs==8192 || cs==16384 || cs==32768 ||
	    cs==65536 || cs==131072 || cs==262144 ||
	    cs==524288 || cs==1048576)
	{
		return 0;
	}
	return -1;
}

int _clustersize_to_string_u64(GString **retval, void *top, typeinfo *info)
{
	__u64 csize;
	
	csize = G_STRUCT_MEMBER(__u64, top, info->off);

	if (_clustersize_valid(top, info) != -1)
	{
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%llu", csize);
		return 0;
	}

#ifdef SHOW_INVALID_VALUES
	*retval = g_string_new("");
	g_string_sprintf(*retval, "%llu", csize);
#endif
	return -1;
}

int _string_to_clustersize_u64(char *newval, void *top, typeinfo *info)
{
	__u64 csize;
	
	csize = strtoull (newval, NULL, 10);
	if (csize==4096 || csize==8192 || csize==16384 || csize==32768 ||
	    csize==65536 || csize==131072 || csize==262144 ||
	    csize==524288 || csize==1048576)
	{
		G_STRUCT_MEMBER(__u64, top, info->off) = csize;
		return 0;
	}

	return -1;
}

char * _get_clustersize_helptext(typeinfo *info)
{
	return strdup("one of 4096,8192,16384,32768,65536,131072,262144,524288,1048576");
}



// DATE: __u64
int _date_valid(void *top, typeinfo *info)
{
	return 0;
}

int _date_to_string_u64(GString **retval, void *top, typeinfo *info)
{
	__u64 sec;
	char *t, *t2;

	sec = G_STRUCT_MEMBER(__u64, top, info->off);
	if (_date_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%llu", sec);
#endif
		return -1;
	}
	*retval = g_string_new("");
	t = ctime((time_t *) &sec);
	t2 = t;
	while (*t2)
	{
       		if (*t2=='\n' || *t2=='\r')
			*t2 = ' ';
		t2++;
	}
	g_string_append(*retval, t);
	return 0;
}

int _string_to_date_u64(char *newval, void *top, typeinfo *info)
{
	__u64 sec;
	struct tm t;
	int ret = -1;
	char mo[4];
	int d, y, h, mi, s, i;
	char *months[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", 
			   "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

	memset(&t, 0, sizeof(struct tm));

	if (sscanf(newval, "%3s %d %d %d:%d:%d", mo, &d, &y, &h, &mi, &s) == 6)
	{
		g_strup(mo);
		if (d<=31 && y>=1900 && h<=23 && mi<=59 && s<=59)
		{
			for (i=0; i<12; i++)
			{
				if (strcmp(mo, months[i])==0)
				{
					t.tm_mon = i+1;
					t.tm_sec = s;
					t.tm_min = mi;
					t.tm_hour = h;
					t.tm_year = y-1900;
					sec = mktime(&t);
					G_STRUCT_MEMBER(__u64, top, info->off) = sec;
					ret = 0;
				}
			}
		}
	}

	return ret;
}

char * _get_date_helptext(typeinfo *info)
{
	return strdup("Jan 28 2003 22:30:32");
}


// DIRFLAG: __u8
int _dirflag_valid(void *top, typeinfo *info)
{
	__u8 flag = G_STRUCT_MEMBER(__u8, top, info->off);
	if (flag != 0 && flag != DIR_NODE_FLAG_ROOT)
		return -1;
	return 0;			
}

int _dirflag_to_string_u8(GString **retval, void *top, typeinfo *info)
{
	__u8 flag;
	flag = G_STRUCT_MEMBER(__u8, top, info->off);
	if (_dirflag_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", flag);
#endif
		return -1;
	}
	*retval = g_string_new("");
	g_string_append(*retval, flag & DIR_NODE_FLAG_ROOT ? "DIR_NODE_FLAG_ROOT" : "");
	return 0;
}

int _string_to_dirflag_u8(char *newval, void *top, typeinfo *info)
{
	__u8 flag=0;

	if (strcasecmp(newval, "DIR_NODE_FLAG_ROOT")==0)
		flag |= DIR_NODE_FLAG_ROOT;

	G_STRUCT_MEMBER(__u8, top, info->off) = flag;
	return 0;
}

char * _get_dirflag_helptext(typeinfo *info)
{
	return strdup("DIR_NODE_FLAG_ROOT or NONE");
}


// DIRINDEX: __u8[256]
int _dirindex_valid(void *top, typeinfo *info)
{
	// this one is tough since we don't want duplicate
	// values, but we don't know the upper bound here
	// so we can't check
	if (info->array_size != 256)
		return -1;
	
	return 0;	
}

int _dirindex_to_string_u8(GString **retval, void *top, typeinfo *info)
{
	int i;
	__u8 *buf = NULL;
	__u8 *arr;

	if (info->array_size <= 0)
		return -1;

	arr =  G_STRUCT_MEMBER_P(top, info->off);

	if (_dirindex_valid(top, info) == -1) {
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "[%u, ...]", arr[0]);
#endif
		return -1;
	}

	if ((buf = malloc(info->array_size + 1)) == NULL) {
		LOG_ERROR("unable to allocate %d bytes of memory",
			  info->array_size + 1);
		goto bail;
	}

	memcpy(buf, arr, info->array_size);

	*retval = g_string_new("");
	for (i=0; i<info->array_size; i++) {
		g_string_sprintfa((*retval), "%d ", buf[i]);
	}

      bail:
	free(buf);
	return 0;
}

int _string_to_dirindex_u8(char *newval, void *top, typeinfo *info)
{
	int ret = -1;
	char **arr;
	__u8 index[256];
	int i;

	memset(&(index[0]), 0, 256);
	arr = g_strsplit(newval, ":", 0);

	for (i=0; i<256; i++)
	{  
		int tmp;
		if (arr[i]==NULL)
			break;
	        tmp = atoi(arr[i]);
		if (tmp<0 || tmp>255)
			goto bail;
		index[i] = (__u8)tmp;
	}
	memcpy(G_STRUCT_MEMBER(__u8 *, top, info->off), index, 256);
	ret = 0;

bail:
	g_strfreev(arr);
	return ret;
}

char * _get_dirindex_helptext(typeinfo *info)
{
	return strdup("a string like 5:7:1:2:4:255:... with each index between 0 and 255");
}


// DIRNODEINDEX: __s8 __u8
int _dirnodeindex_valid(void *top, typeinfo *info)
{
	return 0;
}

int _dirnodeindex_to_string_s8(GString **retval, void *top, typeinfo *info)
{
	__s8 idx;
	idx = G_STRUCT_MEMBER(__s8, top, info->off);
	if (_dirnodeindex_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%d", idx);
#endif
		return -1;
	}
	*retval = g_string_new("");
	if (idx==INVALID_DIR_NODE_INDEX)
		g_string_append(*retval, "INVALID_DIR_NODE_INDEX");
	else
		g_string_sprintf(*retval, "%d", idx);
	return 0;
}

int _string_to_dirnodeindex_s8(char *newval, void *top, typeinfo *info)
{
	int tmp;
	char *ptr;

	if (strcasecmp(newval, "INVALID_DIR_NODE_INDEX")==0)
		tmp = INVALID_DIR_NODE_INDEX;
	else
	{
		tmp = strtol(newval, &ptr, 10);
		if (ptr==newval || tmp<-1 || tmp>255)
			return -1;
	}

	G_STRUCT_MEMBER(__s8, top, info->off) = (__s8)tmp;
	return 0;
}

/* the signed and unsigned are the same?!  
 * why yes!  because we like to put -1 into the unsigned one too!
 * /me pokes eyes out
 */
int _dirnodeindex_to_string_u8(GString **retval, void *top, typeinfo *info)
{
	return _dirnodeindex_to_string_s8(retval, top, info);
}

int _string_to_dirnodeindex_u8(char *newval, void *top, typeinfo *info)
{
	return _string_to_dirnodeindex_s8(newval, top, info);
}

char * _get_dirnodeindex_helptext(typeinfo *info)
{
	if (info->is_signed)
		return strdup("some number between -1 and 255, or INVALID_DIR_NODE_INDEX");
	else
		return strdup("some number between 0 and 255, or INVALID_DIR_NODE_INDEX");
}



// DISKPTR: __s64 __u64
int _diskptr_valid(void *top, typeinfo *info)
{
	if (info->is_signed)
	{
		__s64 s = G_STRUCT_MEMBER(__s64, top, info->off);
		if (s != INVALID_NODE_POINTER && (s % 512))
			return -1;
	}
	else
	{
		__u64 u = G_STRUCT_MEMBER(__u64, top, info->off);
		if (u != INVALID_NODE_POINTER && (u % 512))
			return -1;
	}
	return 0;
}

// more junk here: (I think) it's really always a __u64 with -1 being a special case :(
int _diskptr_to_string_s64(GString **retval, void *top, typeinfo *info)
{
	__s64 ptr;
	ptr = G_STRUCT_MEMBER(__s64, top, info->off);
	if (_diskptr_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u.%u", HILO(ptr));
#endif
		return -1;
	}
	*retval = g_string_new("");
	if (ptr==INVALID_NODE_POINTER)
		g_string_append(*retval, "INVALID_NODE_POINTER");
	else
		g_string_sprintf(*retval, "%u.%u", HILO(ptr));
	return 0;
}

int _string_to_diskptr_s64(char *newval, void *top, typeinfo *info)
{
	__s64 ptr;
	char *bad;

	if (strcasecmp(newval, "INVALID_NODE_POINTER")==0)
		ptr = INVALID_NODE_POINTER;
	else
	{
		ptr = strtoll(newval, &bad, 10);
		if (bad==newval || ptr<-1)
			return -1;
	}	
	G_STRUCT_MEMBER(__s64, top, info->off) = ptr;
	return 0;
}

int _diskptr_to_string_u64(GString **retval, void *top, typeinfo *info)
{
	return _diskptr_to_string_s64(retval, top, info);
}

int _string_to_diskptr_u64(char *newval, void *top, typeinfo *info)
{
	return _string_to_diskptr_s64(newval, top, info);
}

char * _get_diskptr_helptext(typeinfo *info)
{
	return strdup("a 64-bit offset, or INVALID_NODE_POINTER");
}


// EXTENTTYPE: __u32
int _extenttype_valid(void *top, typeinfo *info)
{
	__u32 type = G_STRUCT_MEMBER(__u32, top, info->off);
	if (type != OCFS_EXTENT_DATA && type != OCFS_EXTENT_HEADER)
		return -1;
	return 0;
}

int _extenttype_to_string_u32(GString **retval, void *top, typeinfo *info)
{
	__u32 type;
	type = G_STRUCT_MEMBER(__u32, top, info->off);

	if (_extenttype_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", type);
#endif
		return -1;
	}

	if (type == OCFS_EXTENT_DATA)
		*retval = g_string_new("OCFS_EXTENT_DATA");
	else
		*retval = g_string_new("OCFS_EXTENT_HEADER");

	return 0;
}

int _string_to_extenttype_u32(char *newval, void *top, typeinfo *info)
{
	__u32 type;

	if (strcasecmp(newval, "OCFS_EXTENT_HEADER")==0)
		type = OCFS_EXTENT_HEADER;
	else if (strcasecmp(newval, "OCFS_EXTENT_DATA")==0)
		type = OCFS_EXTENT_DATA;
	else 
		return -1;

	G_STRUCT_MEMBER(__u32, top, info->off) = type;

	return 0;
}

char * _get_extenttype_helptext(typeinfo *info)
{
	return strdup("OCFS_EXTENT_HEADER or OCFS_EXTENT_DATA");
}


// FILEFLAG: __u32
int _fileflag_valid(void *top, typeinfo *info)
{
	__u32 flag = G_STRUCT_MEMBER(__u32, top, info->off);
	if (flag!=0 && ! (flag & (FLAG_FILE_CREATE|FLAG_FILE_EXTEND|
				  FLAG_FILE_DELETE|FLAG_FILE_RENAME|
				  FLAG_FILE_UPDATE|FLAG_FILE_CREATE_DIR|
				  FLAG_FILE_UPDATE_OIN|FLAG_FILE_RELEASE_MASTER|
				  FLAG_FILE_RELEASE_CACHE|FLAG_FILE_CREATE_CDSL|
				  FLAG_FILE_DELETE_CDSL|FLAG_FILE_CHANGE_TO_CDSL|
				  FLAG_FILE_TRUNCATE|FLAG_FILE_ACQUIRE_LOCK|
				  FLAG_FILE_RELEASE_LOCK)))
		return -1;
	return 0;
}

int _fileflag_to_string_u32(GString **retval, void *top, typeinfo *info)
{
	__u32 flag;

	flag = G_STRUCT_MEMBER(__u32, top, info->off);
	if (_fileflag_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", flag);
#endif
		return -1;
	}
	    
	*retval = g_string_new("");
	if (flag & FLAG_FILE_CREATE)
		g_string_append(*retval, "FLAG_FILE_CREATE ");
	if (flag & FLAG_FILE_EXTEND)
		g_string_append(*retval, "FLAG_FILE_EXTEND ");
	if (flag & FLAG_FILE_DELETE)
		g_string_append(*retval, "FLAG_FILE_DELETE ");
	if (flag & FLAG_FILE_RENAME)
		g_string_append(*retval, "FLAG_FILE_RENAME ");
	if (flag & FLAG_FILE_UPDATE)
		g_string_append(*retval, "FLAG_FILE_UPDATE ");
	if (flag & FLAG_FILE_CREATE_DIR)
		g_string_append(*retval, "FLAG_FILE_CREATE_DIR ");
	if (flag & FLAG_FILE_UPDATE_OIN)
		g_string_append(*retval, "FLAG_FILE_UPDATE_OIN ");
	if (flag & FLAG_FILE_RELEASE_MASTER)
		g_string_append(*retval, "FLAG_FILE_RELEASE_MASTER ");
	if (flag & FLAG_FILE_RELEASE_CACHE)
		g_string_append(*retval, "FLAG_FILE_RELEASE_CACHE ");
	if (flag & FLAG_FILE_CREATE_CDSL)
		g_string_append(*retval, "FLAG_FILE_CREATE_CDSL ");
	if (flag & FLAG_FILE_DELETE_CDSL)
		g_string_append(*retval, "FLAG_FILE_DELETE_CDSL ");
	if (flag & FLAG_FILE_CHANGE_TO_CDSL)
		g_string_append(*retval, "FLAG_FILE_CHANGE_TO_CDSL ");
	if (flag & FLAG_FILE_TRUNCATE)
		g_string_append(*retval, "FLAG_FILE_TRUNCATE ");
	if (flag & FLAG_FILE_ACQUIRE_LOCK)
		g_string_append(*retval, "FLAG_FILE_ACQUIRE_LOCK");
	if (flag & FLAG_FILE_RELEASE_LOCK)
		g_string_append(*retval, "FLAG_FILE_RELEASE_LOCK");
	if ((*retval)->len > 0)
		g_string_truncate(*retval, (*retval)->len - 1);

	return 0;
}

int _string_to_fileflag_u32(char *newval, void *top, typeinfo *info)
{
	int ret = -1;
	char **arr, **p;
	__u32 flag;

	flag = 0;
	p = arr = g_strsplit(newval, " ", 0);

	while (*arr!=NULL)
	{  
		if (strcasecmp(*arr, "FLAG_FILE_CREATE")==0)
			flag |= FLAG_FILE_CREATE;
		else if (strcasecmp(*arr, "FLAG_FILE_EXTEND")==0)
			flag |= FLAG_FILE_EXTEND;
		else if (strcasecmp(*arr, "FLAG_FILE_DELETE")==0)
			flag |= FLAG_FILE_DELETE;
		else if (strcasecmp(*arr, "FLAG_FILE_RENAME")==0)
			flag |= FLAG_FILE_RENAME;
		else if (strcasecmp(*arr, "FLAG_FILE_UPDATE")==0)
			flag |= FLAG_FILE_UPDATE;
		else if (strcasecmp(*arr, "FLAG_FILE_CREATE_DIR")==0)
			flag |= FLAG_FILE_CREATE_DIR;
		else if (strcasecmp(*arr, "FLAG_FILE_UPDATE_OIN")==0)
			flag |= FLAG_FILE_UPDATE_OIN;
		else if (strcasecmp(*arr, "FLAG_FILE_RELEASE_MASTER")==0)
			flag |= FLAG_FILE_RELEASE_MASTER;
		else if (strcasecmp(*arr, "FLAG_FILE_RELEASE_CACHE")==0)
			flag |= FLAG_FILE_RELEASE_CACHE;
		else if (strcasecmp(*arr, "FLAG_FILE_CREATE_CDSL")==0)
			flag |= FLAG_FILE_CREATE_CDSL;
		else if (strcasecmp(*arr, "FLAG_FILE_DELETE_CDSL")==0)
			flag |= FLAG_FILE_DELETE_CDSL;
		else if (strcasecmp(*arr, "FLAG_FILE_CHANGE_TO_CDSL")==0)
			flag |= FLAG_FILE_CHANGE_TO_CDSL;
		else if (strcasecmp(*arr, "FLAG_FILE_TRUNCATE")==0)
			flag |= FLAG_FILE_TRUNCATE;
		else if (strcasecmp(*arr, "FLAG_FILE_ACQUIRE_LOCK")==0)
			flag |= FLAG_FILE_ACQUIRE_LOCK;
		else if (strcasecmp(*arr, "FLAG_FILE_RELEASE_LOCK")==0)
			flag |= FLAG_FILE_RELEASE_LOCK;
		arr++;
	}
	G_STRUCT_MEMBER(__u32, top, info->off) = flag;
	ret = 0;
	g_strfreev(p);
	return ret;
}

char * _get_fileflag_helptext(typeinfo *info)
{
	return strdup("one or more of: FLAG_FILE_CREATE FLAG_FILE_EXTEND "
		      "FLAG_FILE_DELETE FLAG_FILE_RENAME FLAG_FILE_UPDATE "
		      "FLAG_FILE_CREATE_DIR FLAG_FILE_UPDATE_OIN "
		      "FLAG_FILE_RELEASE_MASTER FLAG_FILE_RELEASE_CACHE "
		      "FLAG_FILE_CREATE_CDSL FLAG_FILE_DELETE_CDSL "
		      "FLAG_FILE_CHANGE_TO_CDSL FLAG_FILE_TRUNCATE "
		      "FLAG_FILE_ACQUIRE_LOCK FLAG_FILE_RELEASE_LOCK");
}

// GID: __u32
int _gid_valid(void *top, typeinfo *info)
{
#ifdef ARE_WE_SURE_WE_WANT_TO_DO_THIS
	struct group *gr;
	__u32 id = G_STRUCT_MEMBER(__u32, top, info->off);

	gr = getgrgid(id);
	if (gr == NULL)
		return -1;
#endif
	return 0;
}

int _gid_to_string_u32(GString **retval, void *top, typeinfo *info)
{
	__u32 id;
	struct group *gr;

	*retval = g_string_new("");
	id = G_STRUCT_MEMBER(__u32, top, info->off);
	
	if (_gid_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", id);
#endif
		return -1;
	}
	gr = getgrgid(id);
	g_string_sprintf(*retval, "%u (%s)", id,
			 gr ? gr->gr_name : "unknown group id");
	return 0;
}

int _string_to_gid_u32(char *newval, void *top, typeinfo *info)
{
	__u32 id;
	char *bad = NULL;
	struct group *gr;

	gr = getgrnam(newval);
	if (gr == NULL)
	{
		id = strtoul(newval, &bad, 10);
		if (bad == newval)
			return -1;
	}
	else
		id = gr->gr_gid;

	G_STRUCT_MEMBER(__u32, top, info->off) = id;
	return 0;
}

char * _get_gid_helptext(typeinfo *info)
{
	return strdup("a numeric gid");
}

// UID: __u32
int _uid_valid(void *top, typeinfo *info)
{
#ifdef ARE_WE_SURE_WE_WANT_TO_DO_THIS
	struct passwd *pw;
	__u32 id =  G_STRUCT_MEMBER(__u32, top, info->off);

	pw = getpwuid(id);
	if (pw == NULL)
		return -1;
#endif
	return 0;
}

int _uid_to_string_u32(GString **retval, void *top, typeinfo *info)
{
	__u32 id;
	struct passwd *pw;

	*retval = g_string_new("");
	id = G_STRUCT_MEMBER(__u32, top, info->off);
	if (_uid_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", id);
#endif
		return -1;
	}
	pw = getpwuid(id);
	g_string_sprintf(*retval, "%u (%s)", id,
			 pw ? pw->pw_name : "unknown user id");
	return 0;
}

int _string_to_uid_u32(char *newval, void *top, typeinfo *info)
{
	__u32 id;
	char *bad = NULL;
	struct passwd *pw;

	pw = getpwnam(newval);
	if (pw == NULL)
	{
		id = strtoul(newval, &bad, 10);
		if (bad == newval)
			return -1;
	}
	else
		id = pw->pw_uid;

	G_STRUCT_MEMBER(__u32, top, info->off) = id;
	return 0;
}

char * _get_uid_helptext(typeinfo *info)
{
	return strdup("a numeric uid");
}

// LOCKLEVEL: __u8
int _locklevel_valid(void *top, typeinfo *info)
{
	__u8 lvl = G_STRUCT_MEMBER(__u8, top, info->off);
	if (lvl != OCFS_DLM_SHARED_LOCK && lvl != OCFS_DLM_EXCLUSIVE_LOCK &&
	    lvl != OCFS_DLM_ENABLE_CACHE_LOCK && lvl != OCFS_DLM_NO_LOCK)
		return -1;
	return 0;
}

int _locklevel_to_string_u8(GString **retval, void *top, typeinfo *info)
{
	__u8 lvl;

	lvl = G_STRUCT_MEMBER(__u8, top, info->off);
	if (_locklevel_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", lvl);
#endif
		return -1;
	}

	*retval = g_string_new("");
	switch (lvl)
	{
		case OCFS_DLM_ENABLE_CACHE_LOCK:
			g_string_append(*retval, "OCFS_DLM_ENABLE_CACHE_LOCK");
			break;
		case OCFS_DLM_EXCLUSIVE_LOCK: 
			g_string_append(*retval, "OCFS_DLM_EXCLUSIVE_LOCK");
			break;
		case OCFS_DLM_SHARED_LOCK:
			g_string_append(*retval, "OCFS_DLM_SHARED_LOCK");
			break;
		case OCFS_DLM_NO_LOCK:
			g_string_append(*retval, "OCFS_DLM_NO_LOCK");
			break;
	}
	return 0;
}

int _string_to_locklevel_u8(char *newval, void *top, typeinfo *info)
{
	__u8 lvl; 

	if (strcasecmp(newval, "OCFS_DLM_NO_LOCK")==0)
		lvl = OCFS_DLM_NO_LOCK;
	else if (strcasecmp(newval, "OCFS_DLM_SHARED_LOCK")==0)
		lvl = OCFS_DLM_SHARED_LOCK;
	else if (strcasecmp(newval, "OCFS_DLM_EXCLUSIVE_LOCK")==0)
		lvl = OCFS_DLM_EXCLUSIVE_LOCK;
	else if (strcasecmp(newval, "OCFS_DLM_ENABLE_CACHE_LOCK")==0)
		lvl = OCFS_DLM_ENABLE_CACHE_LOCK;
	else
		return -1;
	
	G_STRUCT_MEMBER(__u8, top, info->off) = lvl;
	return 0;

}

char * _get_locklevel_helptext(typeinfo *info)
{
	return strdup("one of OCFS_DLM_ENABLE_CACHE_LOCK OCFS_DLM_EXCLUSIVE_LOCK "
		      "OCFS_DLM_SHARED_LOCK OCFS_DLM_NO_LOCK");
}

// NODEBITMAP: __u64
int _nodebitmap_valid(void *top, typeinfo *info)
{
	__u64 bm = G_STRUCT_MEMBER(__u64, top, info->off);
	// should have nothing in the upper 32 bits
	if (bm & 0xffffffff00000000ULL)
		return -1;
	return 0;
}

int _nodebitmap_to_string_u64(GString **retval, void *top, typeinfo *info)
{
	__u64 bm;
	int pos;

	bm = G_STRUCT_MEMBER(__u64, top, info->off);

	if (_nodebitmap_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%llu", bm);
#endif
		return -1;
	}

	*retval = g_string_new("");	
	for (pos=0; pos<32; pos++)
		g_string_sprintfa(*retval, "%d", (bm & (1 << pos)) ? 1 : 0);

	return 0;
}

int _string_to_nodebitmap_u64(char *newval, void *top, typeinfo *info)
{
	__u64 bm;
	char *bad = NULL;

	bm = strtol(newval, &bad, 2);
	if (bad == newval)
		return -1;
	G_STRUCT_MEMBER(__u64, top, info->off) = bm;
	return 0;
}

char * _get_nodebitmap_helptext(typeinfo *info)
{
	return strdup("a 32-node binary map like: "
		      "01101010011101010101010100101100");
}

// NODENUM: __u32 __s32
int _nodenum_valid(void *top, typeinfo *info)
{
	__s32 snum = G_STRUCT_MEMBER(__s32, top, info->off);
	__u32 unum = G_STRUCT_MEMBER(__u32, top, info->off);

	if (info->is_signed)
	{
		if (snum < OCFS_INVALID_NODE_NUM || snum > 31)
			return -1;
	}
	else
	{
		if (unum > 31 && unum != OCFS_INVALID_NODE_NUM)
			return -1;
	}
			
	return 0;
}

int _nodenum_to_string_u32(GString **retval, void *top, typeinfo *info)
{
	__u32 num;

	num = G_STRUCT_MEMBER(__u32, top, info->off);
	if (_nodenum_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", num);
#endif
		return -1;
	}

	*retval = g_string_new("");
	if (num == OCFS_INVALID_NODE_NUM)
		g_string_append(*retval, "OCFS_INVALID_NODE_NUM");
	else
		g_string_sprintf(*retval, "%u", num);

	return 0;
}

int _string_to_nodenum_u32(char *newval, void *top, typeinfo *info)
{
	__u32 num;
	char *bad = NULL;

	if (strcasecmp(newval, "OCFS_INVALID_NODE_NUM")==0)
		num = OCFS_INVALID_NODE_NUM;
	else
		num = strtoul(newval, &bad, 10);
	if (bad == newval || num<0 || num>31)
		return -1;
	G_STRUCT_MEMBER(__u32, top, info->off) = num;
	return 0;
}

int _nodenum_to_string_s32(GString **retval, void *top, typeinfo *info)
{
	__s32 num;

	num = G_STRUCT_MEMBER(__s32, top, info->off);
	if (_nodenum_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%d", num);
#endif
		return -1;
	}

	*retval = g_string_new("");
	if (num == OCFS_INVALID_NODE_NUM)
		g_string_append(*retval, "OCFS_INVALID_NODE_NUM");
	else
		g_string_sprintf(*retval, "%d", num);

	return 0;
}

int _string_to_nodenum_s32(char *newval, void *top, typeinfo *info)
{
	__s32 num;
	char *bad = NULL;

	if (strcasecmp(newval, "OCFS_INVALID_NODE_NUM")==0)
		num = OCFS_INVALID_NODE_NUM;
	else
		num = strtol(newval, &bad, 10);
	if (bad == newval || num<-1 || num>31)
		return -1;
	G_STRUCT_MEMBER(__s32, top, info->off) = num;
	return 0;
}

char * _get_nodenum_helptext(typeinfo *info)
{
	return strdup("a node number between 0 and 31, or OCFS_INVALID_NODE_NUM");
}

// PERMS: __u32
int _perms_valid(void *top, typeinfo *info)
{
	return 0;	
}

int _perms_to_string_u32(GString **retval, void *top, typeinfo *info)
{
	__u32 mode;

	mode = G_STRUCT_MEMBER(__u32, top, info->off);
	if (_perms_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", mode);
#endif
		return -1;
	}

	*retval = g_string_sized_new(10);
	g_string_append_c(*retval, '-');
	g_string_append_c(*retval, mode & S_IRUSR ? 'r' : '-');
	g_string_append_c(*retval, mode & S_IWUSR ? 'w' : '-');
	if (mode & S_ISUID)
		g_string_append_c(*retval, mode & S_IXUSR ? 's' : 'S');
	else
		g_string_append_c(*retval, mode & S_IXUSR ? 'x' : '-');
	
	g_string_append_c(*retval, mode & S_IRGRP ? 'r' : '-');
	g_string_append_c(*retval, mode & S_IWGRP ? 'w' : '-');
	if (mode & S_ISGID)
		g_string_append_c(*retval, mode & S_IXGRP ? 's' : 'S');
	else
		g_string_append_c(*retval, mode & S_IXGRP ? 'x' : '-');
	
	g_string_append_c(*retval, mode & S_IROTH ? 'r' : '-');
	g_string_append_c(*retval, mode & S_IWOTH ? 'w' : '-');
	if (mode & S_ISVTX)
		g_string_append_c(*retval, mode & S_IXOTH ? 't' : 'T');
	else
		g_string_append_c(*retval, mode & S_IXOTH ? 'x' : '-');
	return 0;
}

int _string_to_perms_u32(char *newval, void *top, typeinfo *info)
{
	int mode = 0;

	if (strlen(newval)!=10) 
		return -1;

	if (newval[1] == 'r')
		mode |= S_IRUSR;
	if (newval[2] == 'w')
		mode |= S_IWUSR;
	if (newval[3] == 'x' || newval[3] == 's')
		mode |= S_IXUSR;
	if (newval[3] == 's' || newval[3] == 'S')
		mode |= S_ISUID;

	if (newval[4] == 'r')
		mode |= S_IRGRP;
	if (newval[5] == 'w')
		mode |= S_IWGRP;
	if (newval[6] == 'x' || newval[6] == 's')
		mode |= S_IXGRP;
	if (newval[6] == 's' || newval[6] == 'S')
		mode |= S_ISGID;

	if (newval[7] == 'r')
		mode |= S_IROTH;
	if (newval[8] == 'w')
		mode |= S_IWOTH;
	if (newval[9] == 'x' || newval[9] == 't')
		mode |= S_IXOTH;
	if (newval[9] == 't' || newval[9] == 'T')
		mode |= S_ISVTX;

	G_STRUCT_MEMBER(__u32, top, info->off) = mode;

	return 0;
}

char * _get_perms_helptext(typeinfo *info)
{
	return 	strdup("-rwxrwxrwx  (filetype ignored; x,s,S,t,T allowed)");
}

// SYNCFLAG: __u32 __u8
int _syncflag_valid(void *top, typeinfo *info)
{
	__u32 sync = G_STRUCT_MEMBER(__u32, top, info->off);
	if (sync !=0 && !(sync & (OCFS_SYNC_FLAG_VALID|
				  OCFS_SYNC_FLAG_CHANGE|
				  OCFS_SYNC_FLAG_MARK_FOR_DELETION|
				  OCFS_SYNC_FLAG_NAME_DELETED)))
		return -1;
	return 0;
}

int _syncflag_to_string_u32(GString **retval, void *top, typeinfo *info)
{
	__u32 sync;
	sync = G_STRUCT_MEMBER(__u32, top, info->off);
	if (_syncflag_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", sync);
#endif
		return -1;
	}
	
	*retval = g_string_new("");
	if (sync == OCFS_SYNC_FLAG_DELETED)
		g_string_append(*retval, "deleted");
	if (sync & OCFS_SYNC_FLAG_VALID)
		g_string_append(*retval, "valid ");
	if (sync & OCFS_SYNC_FLAG_CHANGE)
		g_string_append(*retval, "change ");
	if (sync & OCFS_SYNC_FLAG_MARK_FOR_DELETION)
		g_string_append(*retval, "mark-del ");
	if (sync & OCFS_SYNC_FLAG_NAME_DELETED)
		g_string_append(*retval, "name-del ");
	return 0;
}

int _string_to_syncflag_u32(char *newval, void *top, typeinfo *info)
{
	__u32 sync = 0;
	int ret = -1;
	char **arr, **p;

	p = arr = g_strsplit(newval, " ", 0);

	while (*arr != NULL)
	{
		if (strcasecmp(*arr, "deleted")==0)
			sync |= OCFS_SYNC_FLAG_DELETED;
		else if (strcasecmp(*arr, "valid")==0)
			sync |= OCFS_SYNC_FLAG_VALID;
		else if (strcasecmp(*arr, "change")==0)
			sync |= OCFS_SYNC_FLAG_CHANGE;
		else if (strcasecmp(*arr, "mark-del")==0)
			sync |= OCFS_SYNC_FLAG_MARK_FOR_DELETION;
		else if (strcasecmp(*arr, "name-del")==0)
			sync |= OCFS_SYNC_FLAG_NAME_DELETED;
		else
			goto bail;
		arr++;
	}
	G_STRUCT_MEMBER(__u32, top, info->off) = sync;
	ret = 0;
bail:
	g_strfreev(p);
	return ret;
}

int _syncflag_to_string_u8(GString **retval, void *top, typeinfo *info)
{
	__u8 sync;
	sync = G_STRUCT_MEMBER(__u8, top, info->off);
	if (_syncflag_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", sync);
#endif
		return -1;
	}

	*retval = g_string_new("");
	if (sync == OCFS_SYNC_FLAG_DELETED)
		g_string_append(*retval, "deleted");
	if (sync & OCFS_SYNC_FLAG_VALID)
		g_string_append(*retval, "valid ");
	if (sync & OCFS_SYNC_FLAG_CHANGE)
		g_string_append(*retval, "change ");
	if (sync & OCFS_SYNC_FLAG_MARK_FOR_DELETION)
		g_string_append(*retval, "mark-del ");
	if (sync & OCFS_SYNC_FLAG_NAME_DELETED)
		g_string_append(*retval, "name-del ");
	return 0;
}

int _string_to_syncflag_u8(char *newval, void *top, typeinfo *info)
{
	__u8 sync = 0;
	int ret = -1;
	char **arr, **p;

	sync = 0;
	p = arr = g_strsplit(newval, " ", 0);

	while (*arr != NULL)
	{
		if (strcasecmp(*arr, "deleted")==0)
			sync |= OCFS_SYNC_FLAG_DELETED;
		else if (strcasecmp(*arr, "valid")==0)
			sync |= OCFS_SYNC_FLAG_VALID;
		else if (strcasecmp(*arr, "change")==0)
			sync |= OCFS_SYNC_FLAG_CHANGE;
		else if (strcasecmp(*arr, "mark-del")==0)
			sync |= OCFS_SYNC_FLAG_MARK_FOR_DELETION;
		else if (strcasecmp(*arr, "name-del")==0)
			sync |= OCFS_SYNC_FLAG_NAME_DELETED;
		else
			goto bail;
		arr++;
	}
	G_STRUCT_MEMBER(__u8, top, info->off) = sync;
	ret = 0;
bail:
	g_strfreev(p);
	return ret;

}

char * _get_syncflag_helptext(typeinfo *info)
{
	return strdup("one or more of: deleted valid change mark-del name-del");
}

// CHAR ARRAY: __u8[]
int _char_array_valid(void *top, typeinfo *info)
{
	return 0;
}

int _char_array_to_string_u8(GString **retval, void *top, typeinfo *info)
{
	char *buf = NULL;
	__u8 *arr;

	if (info->array_size <= 0)
		return -1;
	arr =  G_STRUCT_MEMBER_P(top, info->off);
	if (_char_array_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "[ %u, ... ]", arr[0]);
#endif
		return -1;
	}
	
	if ((buf = malloc(info->array_size + 1)) == NULL) {
		LOG_ERROR("unable to allocate %d bytes of memory",
			  info->array_size + 1);
		goto bail;
	}

	memcpy(buf, arr, info->array_size);
	buf[ info->array_size ] = '\0';
	*retval = g_string_new(buf);

      bail:
	free(buf);
	return 0;
}

int _string_to_char_array_u8(char *newval, void *top, typeinfo *info)
{
	char *buf;
	if (info->array_size <= 0)
		return -1;

	buf = G_STRUCT_MEMBER_P(top, info->off);
	strncpy(buf, newval, info->array_size);
	buf[info->array_size - 1] = '\0';
	return 0;
}

char * _get_char_array_helptext(typeinfo *info)
{
	return g_strdup_printf("a string with maximum length %d",
			       info->array_size - 1);
}

// HEX ARRAY: __u8[]
int _hex_array_valid(void *top, typeinfo *info)
{
	return 0;
}

int _hex_array_to_string_u8(GString **retval, void *top, typeinfo *info)
{
	char *buf = NULL;
	char *p;
	int i;
	__u8 *arr;

	if (info->array_size <= 0)
		return -1;
	arr =  G_STRUCT_MEMBER_P(top, info->off);
	if (_char_array_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "[ %X, ... ]", arr[0]);
#endif
		return -1;
	}
	
	if ((buf = malloc(2 * info->array_size + 1)) == NULL) {
		LOG_ERROR("unable to allocate %d bytes of memory",
			  info->array_size + 1);
		goto bail;
	}

	for (i = 0, p = buf; i < info->array_size; ++i, p += 2)
		sprintf(p, "%2X", arr[i]);
	buf[ 2 * info->array_size ] = '\0';
	*retval = g_string_new(buf);

      bail:
	free(buf);
	return 0;
}

int _string_to_hex_array_u8(char *newval, void *top, typeinfo *info)
{
	unsigned char *buf;
	unsigned char tb[3];
	int i;
	unsigned char *p;

	if (info->array_size <= 0)
		return -1;

	buf = G_STRUCT_MEMBER_P(top, info->off);

	tb[2] = '\0';
	for (i = 0, p = newval; i < info->array_size; ++i, ++p) {
		tb[0] = *p;
		tb[1] = *(++p);
		buf[i] = strtoul(tb, NULL, 16);
	}

	return 0;
}

char * _get_hex_array_helptext(typeinfo *info)
{
	return g_strdup_printf("a hex string with maximum length %d",
			       info->array_size);
}

// NUMBER RANGE: __s32 __u8 __u16 __u32 __u64
int _number_range_valid(void *top, typeinfo *info) 
{
	if (info->is_signed)
	{
		__s64 num, hi, lo;
		switch (info->type_size)
		{
		case 1:
			num = G_STRUCT_MEMBER(__s8, top, info->off);
			hi = (__s8)(info->hi & 0xffLL);
			lo = (__s8)(info->lo & 0xffLL);
			break;
		case 2:
			num = G_STRUCT_MEMBER(__s16, top, info->off);
			hi = (__s16)(info->hi & 0xffffLL);
			lo = (__s16)(info->lo & 0xffffLL);
			break;
		case 4:
			num = G_STRUCT_MEMBER(__s32, top, info->off);
			hi = (__s32)(info->hi & 0xffffffffLL);
			lo = (__s32)(info->lo & 0xffffffffLL);
			break;
		case 8:
			num = G_STRUCT_MEMBER(__s64, top, info->off);
			hi = (__s64)info->hi;
			lo = (__s64)info->lo;
			break;
		default:
			return -1;
			break;
		}
		if (num < lo || num > hi)
			return -1;
	}
	else
	{
		__u64 num, hi, lo;
		switch (info->type_size)
		{
		case 1:
			num = G_STRUCT_MEMBER(__u8, top, info->off);
			hi = (__u8)(info->hi & 0xffLL);
			lo = (__u8)(info->lo & 0xffLL);
			break;
		case 2:
			num = G_STRUCT_MEMBER(__u16, top, info->off);
			hi = (__u16)(info->hi & 0xffffLL);
			lo = (__u16)(info->lo & 0xffffLL);
			break;
		case 4:
			num = G_STRUCT_MEMBER(__u32, top, info->off);
			hi = (__u32)(info->hi & 0xffffffffLL);
			lo = (__u32)(info->lo & 0xffffffffLL);
			break;
		case 8:
			num = G_STRUCT_MEMBER(__u64, top, info->off);
			hi = (__u64)info->hi;
			lo = (__u64)info->lo;
			break;
		default:
			return -1;
			break;
		}
		if (num < lo || num > hi)
			return -1;

	}
	return 0;
}

int _number_range_to_string_s32(GString **retval, void *top,
				typeinfo *info)
{
	__s32 num;

	num = G_STRUCT_MEMBER(__s32, top, info->off);
	if (_number_range_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%d", num);
#endif
		return -1;
	}
	*retval = g_string_new("");
	g_string_sprintfa(*retval, "%d", num);
	return 0;
}

int _string_to_number_range_s32(char *newval, void *top, typeinfo *info)
{
	__s32 num, hi, lo;
	char *bad = NULL;

	hi = (__s32)(info->hi & 0xffffffffULL);
	lo = (__s32)(info->lo & 0xffffffffULL);
	
	num = strtol(newval, &bad, 10);
	if (bad == newval || num < lo || num > hi)
		return -1;
	G_STRUCT_MEMBER(__s32, top, info->off) = num;
	return 0;
}

int _number_range_to_string_u8(GString **retval, void *top, typeinfo *info)
{
	__u8 num;

	num = G_STRUCT_MEMBER(__u8, top, info->off);
	if (_number_range_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", num);
#endif
		return -1;
	}
	
	*retval = g_string_new("");
	g_string_sprintfa(*retval, "%u", num);
	return 0;
}

int _string_to_number_range_u8(char *newval, void *top, typeinfo *info)
{
	__u32 tmp;
	__u8 hi, lo;
	char *bad = NULL;

	hi = (__u8)(info->hi & 0xffULL);
	lo = (__u8)(info->lo & 0xffULL);
	
	tmp = strtoul(newval, &bad, 10);
	if (bad == newval || tmp < lo || tmp > hi)
		return -1;
	G_STRUCT_MEMBER(__u8, top, info->off) = tmp;
	return 0;
}

int _number_range_to_string_u16(GString **retval, void *top, typeinfo *info)
{
	__u16 num;

	num = G_STRUCT_MEMBER(__u16, top, info->off);
	if (_number_range_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", num);
#endif
		return -1;
	}
	
	*retval = g_string_new("");
	g_string_sprintfa(*retval, "%u", num);
	return 0;
}

int _string_to_number_range_u16(char *newval, void *top, typeinfo *info)
{
	__u32 tmp;
	__u16 hi, lo;
	char *bad = NULL;

	hi = (__u16)(info->hi & 0xffffULL);
	lo = (__u16)(info->lo & 0xffffULL);
	
	tmp = strtoul(newval, &bad, 10);
	if (bad == newval || tmp < lo || tmp > hi)
		return -1;
	G_STRUCT_MEMBER(__u16, top, info->off) = tmp;
	return 0;
}

int _number_range_to_string_u32(GString **retval, void *top, typeinfo *info)
{
	__u32 num;

	num = G_STRUCT_MEMBER(__u32, top, info->off);
	if (_number_range_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%u", num);
#endif
		return -1;
	}
	
	*retval = g_string_new("");
	g_string_sprintfa(*retval, "%u", num);
	return 0;
}

int _string_to_number_range_u32(char *newval, void *top, typeinfo *info)
{
	__u32 tmp, hi, lo;
	char *bad = NULL;

	hi = (__u32)(info->hi & 0xffffffffULL);
	lo = (__u32)(info->lo & 0xffffffffULL);
	
	tmp = strtoul(newval, &bad, 10);
	if (bad == newval || tmp < lo || tmp > hi)
		return -1;
	G_STRUCT_MEMBER(__u32, top, info->off) = tmp;
	return 0;
}

int _number_range_to_string_u64(GString **retval, void *top, typeinfo *info)
{
	__u64 num;

	num = G_STRUCT_MEMBER(__u64, top, info->off);
	if (_number_range_valid(top, info) == -1)
	{
#ifdef SHOW_INVALID_VALUES
		*retval = g_string_new("");
		g_string_sprintf(*retval, "%llu", num);
#endif
		return -1;
	}
	*retval = g_string_new("");
	g_string_sprintfa(*retval, "%llu", num);
	return 0;
}

int _string_to_number_range_u64(char *newval, void *top, typeinfo *info)
{
	__u64 tmp;
	char *bad = NULL;

	tmp = strtoull(newval, &bad, 10);
	if (bad == newval || tmp < info->lo || tmp > info->hi)
		return -1;
	G_STRUCT_MEMBER(__u64, top, info->off) = tmp;
	return 0;
}

char * _get_number_range_helptext(typeinfo *info)
{
	if (info->is_signed)
		return g_strdup_printf("a number between %d and %d (inclusive)", 
				       (__s32)(info->lo & 0xffffffffULL), 
				       (__s32)(info->hi & 0xffffffffULL));
	else
		return g_strdup_printf("a number between %llu and %llu (inclusive)",
				       info->lo, info->hi);
}

// VOTEFLAG: __u8[32]
int _voteflag_array_valid(void *top, typeinfo *info)
{
	int i;
	__u8 *arr;
	if (info->array_size != 32)
		return -1;

	arr = G_STRUCT_MEMBER_P(top, info->off);
	for (i=0; i<info->array_size; i++)
	{
		if (arr[i] != 0 &&
		    arr[i] != FLAG_VOTE_NODE &&
		    arr[i] != FLAG_VOTE_OIN_UPDATED &&
		    arr[i] != FLAG_VOTE_OIN_ALREADY_INUSE &&
		    arr[i] != FLAG_VOTE_UPDATE_RETRY &&
		    arr[i] != FLAG_VOTE_FILE_DEL)
			return -1;
	}
	return 0;
}

int _voteflag_array_to_string_u8(GString **retval, void *top, typeinfo *info)
{
	__u8 *arr;
	int i;

	if (info->array_size <= 0)
		return -1;

        arr = G_STRUCT_MEMBER_P(top, info->off);
	if (_voteflag_array_valid(top, info) == -1)
		goto fail;

	*retval = g_string_new("");
	for (i=0; i<info->array_size; i++)
	{
		if (i!=0)
			g_string_append(*retval, " ");
		if (arr[i] & FLAG_VOTE_NODE)
			g_string_append(*retval, "vote");
		else if (arr[i] & FLAG_VOTE_OIN_UPDATED)
			g_string_append(*retval, "updated");
		else if (arr[i] & FLAG_VOTE_OIN_ALREADY_INUSE)
			g_string_append(*retval, "inuse");
		else if (arr[i] & FLAG_VOTE_UPDATE_RETRY)
			g_string_append(*retval, "retry");
		else if (arr[i] & FLAG_VOTE_FILE_DEL)
			g_string_append(*retval, "delete");
		else if (arr[i] == 0)
			g_string_append(*retval, "none");
	}

	return 0;
fail:
#ifdef SHOW_INVALID_VALUES
	{
		int j;
		for (j=0; j<info->array_size; j++)
		{
			if (j!=0)
				g_string_append(*retval, ":");
			g_string_sprintfa(*retval, "%u", arr[j]);
		}
	}
#else
	g_string_free(*retval, true);
#endif
	return -1;
}

int _string_to_voteflag_array_u8(char *newval, void *top, typeinfo *info)
{
	__u8 *flag;
	int ret = -1, i;
	char **arr;
	
	if (info->array_size <= 0)
		return -1;

	flag = G_STRUCT_MEMBER(__u8 *, top, info->off);
	arr = g_strsplit(newval, " ", 0);
	
	for (i=0; i<info->array_size && arr[i]!=NULL; i++)
	{
		flag[i] = 0;
		if (strcasecmp(arr[i], "vote")==0)
			flag[i] |= FLAG_VOTE_NODE;
		else if (strcasecmp(arr[i], "updated")==0)
			flag[i] |= FLAG_VOTE_OIN_UPDATED;
		else if (strcasecmp(arr[i], "inuse")==0)
			flag[i] |= FLAG_VOTE_OIN_ALREADY_INUSE;
		else if (strcasecmp(arr[i], "retry")==0)
			flag[i] |= FLAG_VOTE_UPDATE_RETRY;
		else if (strcasecmp(arr[i], "delete")==0)
			flag[i] |= FLAG_VOTE_FILE_DEL;
		else if (strcasecmp(arr[i], "none")==0 || atoi(arr[i])==0)
			flag[i] = 0;
		else
			goto bail;

	}
	ret = 0;
bail:
	g_strfreev(arr);
	return ret;
}

char * _get_voteflag_array_helptext(typeinfo *info)
{
	return g_strdup_printf("one flag for each node (up to %d): "
			       "none vote updated inuse retry delete "
			       "(space-delimited)",
			       info->array_size);
}

