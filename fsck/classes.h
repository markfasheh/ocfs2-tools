/*
 * classes.h
 *
 * Function prototypes, macros, etc. for related 'C' files
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

#ifndef _CLASSES_H
#define _CLASSES_H


typedef struct _typeinfo
{
	int off;
	int array_size;
	int type_size;
	bool is_signed;
	__u64 lo;
	__u64 hi;
} typeinfo;

typedef struct _ocfs_class_member
{
	char *name;
	char *flavor;
	typeinfo type;
	int (*valid) (void *, typeinfo *);
	int (*to_string) (GString **, void *, typeinfo *);
	int (*from_string) (char *, void *, typeinfo *);
	char * (*helptext) (typeinfo *);
} ocfs_class_member;

typedef struct _ocfs_class
{
	char *name;
	int num_members;
	ocfs_class_member *members;
} ocfs_class;

ocfs_class_member * find_class_member(ocfs_class *cl, const char *name, int *idx);

extern ocfs_class ocfs_alloc_ext_class;
extern ocfs_class ocfs_publish_class;
extern ocfs_class ocfs_vote_class;
extern ocfs_class ocfs_file_entry_class;
extern ocfs_class ocfs_dir_node_class;
extern ocfs_class ocfs_extent_group_class;
extern ocfs_class ocfs_vol_disk_hdr_class;
extern ocfs_class ocfs_disk_lock_class;
extern ocfs_class ocfs_vol_label_class;
extern ocfs_class ocfs_ipc_config_info_class;
extern ocfs_class ocfs_guid_class;
extern ocfs_class ocfs_disk_node_config_info_class;
extern ocfs_class ocfs_node_config_hdr_class;

int _attribs_valid(void *top, typeinfo *info);
int _bool_valid(void *top, typeinfo *info);
int _clustersize_valid(void *top, typeinfo *info);
int _date_valid(void *top, typeinfo *info);
int _dirflag_valid(void *top, typeinfo *info);
int _dirindex_valid(void *top, typeinfo *info);
int _dirnodeindex_valid(void *top, typeinfo *info);
int _diskptr_valid(void *top, typeinfo *info);
int _extenttype_valid(void *top, typeinfo *info);
int _fileflag_valid(void *top, typeinfo *info);
int _gid_valid(void *top, typeinfo *info);
int _uid_valid(void *top, typeinfo *info);
int _locklevel_valid(void *top, typeinfo *info);
int _nodebitmap_valid(void *top, typeinfo *info);
int _nodenum_valid(void *top, typeinfo *info);
int _perms_valid(void *top, typeinfo *info);
int _syncflag_valid(void *top, typeinfo *info);
int _char_array_valid(void *top, typeinfo *info);
int _hex_array_valid(void *top, typeinfo *info);
int _number_range_valid(void *top, typeinfo *info) ;
int _voteflag_array_valid(void *top, typeinfo *info);

int _attribs_to_string_u32(GString **retval, void *top, typeinfo *info);
int _string_to_attribs_u32(char *newval, void *top, typeinfo *info);
char * _get_attribs_helptext(typeinfo *info);
int _bool_to_string_s32(GString **retval, void *top, typeinfo *info);
int _string_to_bool_s32(char *newval, void *top, typeinfo *info);
int _bool_to_string_u8(GString **retval, void *top, typeinfo *info);
int _string_to_bool_u8(char *newval, void *top, typeinfo *info);
int _bool_to_string_bool(GString **retval, void *top, typeinfo *info);
int _string_to_bool_bool(char *newval, void *top, typeinfo *info);
char * _get_bool_helptext(typeinfo *info);
int _clustersize_to_string_u64(GString **retval, void *top, typeinfo *info);
int _string_to_clustersize_u64(char *newval, void *top, typeinfo *info);
char * _get_clustersize_helptext(typeinfo *info);
int _date_to_string_u64(GString **retval, void *top, typeinfo *info);
int _string_to_date_u64(char *newval, void *top, typeinfo *info);
char * _get_date_helptext(typeinfo *info);
int _dirflag_to_string_u8(GString **retval, void *top, typeinfo *info);
int _string_to_dirflag_u8(char *newval, void *top, typeinfo *info);
char * _get_dirflag_helptext(typeinfo *info);
int _dirindex_to_string_u8(GString **retval, void *top, typeinfo *info);
int _string_to_dirindex_u8(char *newval, void *top, typeinfo *info);
char * _get_dirindex_helptext(typeinfo *info);
int _dirnodeindex_to_string_s8(GString **retval, void *top, typeinfo *info);
int _string_to_dirnodeindex_s8(char *newval, void *top, typeinfo *info);
int _dirnodeindex_to_string_u8(GString **retval, void *top, typeinfo *info);
int _string_to_dirnodeindex_u8(char *newval, void *top, typeinfo *info);
char * _get_dirnodeindex_helptext(typeinfo *info);
int _diskptr_to_string_s64(GString **retval, void *top, typeinfo *info);
int _string_to_diskptr_s64(char *newval, void *top, typeinfo *info);
int _diskptr_to_string_u64(GString **retval, void *top, typeinfo *info);
int _string_to_diskptr_u64(char *newval, void *top, typeinfo *info);
char * _get_diskptr_helptext(typeinfo *info);
int _extenttype_to_string_u32(GString **retval, void *top, typeinfo *info);
int _string_to_extenttype_u32(char *newval, void *top, typeinfo *info);
char * _get_extenttype_helptext(typeinfo *info);
int _fileflag_to_string_u32(GString **retval, void *top, typeinfo *info);
int _string_to_fileflag_u32(char *newval, void *top, typeinfo *info);
char * _get_fileflag_helptext(typeinfo *info);
int _gid_to_string_u32(GString **retval, void *top, typeinfo *info);
int _string_to_gid_u32(char *newval, void *top, typeinfo *info);
char * _get_gid_helptext(typeinfo *info);
int _uid_to_string_u32(GString **retval, void *top, typeinfo *info);
int _string_to_uid_u32(char *newval, void *top, typeinfo *info);
char * _get_uid_helptext(typeinfo *info);
int _locklevel_to_string_u8(GString **retval, void *top, typeinfo *info);
int _string_to_locklevel_u8(char *newval, void *top, typeinfo *info);
char * _get_locklevel_helptext(typeinfo *info);
int _nodebitmap_to_string_u64(GString **retval, void *top, typeinfo *info);
int _string_to_nodebitmap_u64(char *newval, void *top, typeinfo *info);
char * _get_nodebitmap_helptext(typeinfo *info);
int _nodenum_to_string_u32(GString **retval, void *top, typeinfo *info);
int _string_to_nodenum_u32(char *newval, void *top, typeinfo *info);
int _nodenum_to_string_s32(GString **retval, void *top, typeinfo *info);
int _string_to_nodenum_s32(char *newval, void *top, typeinfo *info);
char * _get_nodenum_helptext(typeinfo *info);
int _perms_to_string_u32(GString **retval, void *top, typeinfo *info);
int _string_to_perms_u32(char *newval, void *top, typeinfo *info);
char * _get_perms_helptext(typeinfo *info);
int _syncflag_to_string_u32(GString **retval, void *top, typeinfo *info);
int _string_to_syncflag_u32(char *newval, void *top, typeinfo *info);
int _syncflag_to_string_u8(GString **retval, void *top, typeinfo *info);
int _string_to_syncflag_u8(char *newval, void *top, typeinfo *info);
char * _get_syncflag_helptext(typeinfo *info);
int _char_array_to_string_u8(GString **retval, void *top, typeinfo *info);
int _string_to_char_array_u8(char *newval, void *top, typeinfo *info);
char * _get_char_array_helptext(typeinfo *info);
int _hex_array_to_string_u8(GString **retval, void *top, typeinfo *info);
int _string_to_hex_array_u8(char *newval, void *top, typeinfo *info);
char * _get_hex_array_helptext(typeinfo *info);
int _number_range_to_string_s32(GString **retval, void *top, typeinfo *info);
int _string_to_number_range_s32(char *newval, void *top, typeinfo *info);
int _number_range_to_string_u8(GString **retval, void *top, typeinfo *info);
int _string_to_number_range_u8(char *newval, void *top, typeinfo *info);
int _number_range_to_string_u16(GString **retval, void *top, typeinfo *info);
int _string_to_number_range_u16(char *newval, void *top, typeinfo *info);
int _number_range_to_string_u32(GString **retval, void *top, typeinfo *info);
int _string_to_number_range_u32(char *newval, void *top, typeinfo *info);
int _number_range_to_string_u64(GString **retval, void *top, typeinfo *info);
int _string_to_number_range_u64(char *newval, void *top, typeinfo *info);
char * _get_number_range_helptext(typeinfo *info);
int _voteflag_array_to_string_u8(GString **retval, void *top, typeinfo *info);
int _string_to_voteflag_array_u8(char *newval, void *top, typeinfo *info);
char * _get_voteflag_array_helptext(typeinfo *info);
int _extent_array_to_string(GString **retval, void *top, typeinfo *info);
int _string_to_extent_array(char *newval, void *top, typeinfo *info);
char * _get_extent_array_helptext(typeinfo *info);
int _disklock_to_string(GString **retval, void *top, typeinfo *info);
int _string_to_disklock(char *newval, void *top, typeinfo *info);
char * _get_disklock_helptext(typeinfo *info);

#endif /* _CLASSES_H */
