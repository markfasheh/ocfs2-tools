/*
 * sig.c
 *
 * class signature checking for ocfs file system check utility
 *
 * Copyright (C) 2002, 2003 Oracle.  All rights reserved.
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
 * Author: Kurt Hackel, Sunil Mushran
 */

#include "fsck.h"


int nodecfghdr_sig_match (char *buf, int idx)
{
	ocfs_node_config_hdr *hdr = (ocfs_node_config_hdr *)buf;
	if (!strncmp (hdr->signature, NODE_CONFIG_HDR_SIGN, NODE_CONFIG_SIGN_LEN))
		return 0;
//	LOG_ERROR("invalid signature '%*s' found in autoconfig header",
//		  NODE_CONFIG_SIGN_LEN, hdr->signature);
//	strcpy (hdr->signature, NODE_CONFIG_HDR_SIGN);
	return -EINVAL;
}

int cleanup_log_sig_match (char *buf, int idx)
{
	return 0;
}

int dir_alloc_bitmap_sig_match (char *buf, int idx)
{
	return 0;
}

int dir_alloc_sig_match (char *buf, int idx)
{
	return 0;
}

int vol_disk_header_sig_match  (char *buf, int idx)
{
	ocfs_vol_disk_hdr *hdr = (ocfs_vol_disk_hdr *)buf;
	if (memcmp (hdr->signature, OCFS_VOLUME_SIGNATURE,
		    strlen (OCFS_VOLUME_SIGNATURE)) == 0)
		return 0;
//	LOG_ERROR("invalid signature '%*s' found in volume header",
//		  strlen(OCFS_VOLUME_SIGNATURE), hdr->signature);
//	strcpy (hdr->signature, OCFS_VOLUME_SIGNATURE);
	return -EINVAL;
}

int disk_lock_sig_match (char *buf, int idx)
{
	return 0;
}

int file_alloc_bitmap_sig_match (char *buf, int idx)
{
	return 0;
}

int file_alloc_sig_match (char *buf, int idx)
{
	return 0;
}

int publish_sector_sig_match (char *buf, int idx)
{
	return 0;
}

int recover_log_sig_match (char *buf, int idx)
{
	return 0;
}

int vol_metadata_log_sig_match (char *buf, int idx)
{
	return 0;
}

int vol_metadata_sig_match (char *buf, int idx)
{
	return 0;
}

int vote_sector_sig_match (char *buf, int idx)
{
	return 0;
}

int dir_node_sig_match (char *buf, int idx)
{
	ocfs_dir_node *dir = (ocfs_dir_node *)buf;
	if (IS_VALID_DIR_NODE(dir))
		return 0;
//	LOG_ERROR("invalid signature '%*s' found in directory",
//		  strlen(OCFS_DIR_NODE_SIGNATURE), dir->signature);
//	strcpy (dir->signature, OCFS_DIR_NODE_SIGNATURE);
	return -EINVAL;
}

int file_entry_sig_match (char *buf, int idx)
{
	ocfs_file_entry *fe = (ocfs_file_entry *)buf;
	if (IS_VALID_FILE_ENTRY(fe))
		return 0;
//	LOG_ERROR("invalid signature '%*s' found in file entry",
//		  strlen(OCFS_FILE_ENTRY_SIGNATURE), fe->signature);
//	strcpy (fe->signature, OCFS_FILE_ENTRY_SIGNATURE);
	return -EINVAL;
}

int extent_header_sig_match (char *buf, int idx)
{
	ocfs_extent_group *ext = (ocfs_extent_group *)buf;
	if (IS_VALID_EXTENT_HEADER(ext))
		return 0;
//	LOG_ERROR("invalid signature '%*s' found in extent header",
//		  strlen(OCFS_EXTENT_HEADER_SIGNATURE), ext->signature);
//	strcpy (ext->signature, OCFS_EXTENT_HEADER_SIGNATURE);
	return -EINVAL;
}

int extent_data_sig_match (char *buf, int idx)
{
	ocfs_extent_group *ext = (ocfs_extent_group *)buf;
	if (IS_VALID_EXTENT_DATA(ext))
		return 0;
//	LOG_ERROR("invalid signature '%*s' found in extent data",
//		  strlen(OCFS_EXTENT_DATA_SIGNATURE), ext->signature);
//	strcpy (ext->signature, OCFS_EXTENT_DATA_SIGNATURE);
	return -EINVAL;
}
