/*
 * debugocfs.c
 *
 * Advanced filesystem debugging/recovery tool and library
 *
 * Copyright (C) 2002 Oracle Corporation.  All rights reserved.
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
 * Author: Kurt Hackel
 */

#include "debugocfs.h"
#include <time.h>

#ifdef LIBDEBUGOCFS
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "glib.h"
#include "libdebugocfs.h"
#endif

__u32 debug_context = 0;
__u32 debug_level = 0;
__u32 debug_exclude = 0;

ocfs_global_ctxt OcfsGlobalCtxt;

__u32 OcfsDebugCtxt = OCFS_DEBUG_CONTEXT_INIT;
__u32 OcfsDebugLevel = OCFS_DEBUG_LEVEL_ERROR;
extern user_args args;


/* fills in boolean arr[] (possibly shifted by off) with ranges supplied in str */
int parse_numeric_range(char *str, int arr[], int min, int max, int off)
{
    char *next_range, *dash;
    int begin, end;
    char *endptr;

    while (1)
    {
	next_range = strsep(&str, ",");
	if (next_range == NULL || *next_range == '\0')
	    break;
	if ((dash = strchr(next_range, '-')) != NULL)
	{
	    if (*(dash + 1) == '\0' || strchr(dash + 1, '-') != NULL)
		return false;
	    *dash = '\0';
	    begin = strtol(next_range, &endptr, 10);
	    if (endptr == next_range || begin < min || begin >= max)
		return false;
	    end = strtol(dash + 1, &endptr, 10);
	    if (endptr == dash + 1 || end < min || end >= max || begin > end)
		return false;
	    while (begin <= end)
		arr[off + begin++] = true;
	}
	else
	{
	    begin = strtol(next_range, &endptr, 10);
	    if (begin < min || begin >= max)
		return false;
	    arr[off + begin] = true;
	}
    }
    return true;
}




/* directory walking functions */

void walk_dir_nodes(int fd, __u64 offset, const char *parent, void *buf)
{
    ocfs_dir_node *dir;
    int i;
    __u64 dirPartOffset;

    dir = (ocfs_dir_node *) malloc_aligned(DIR_NODE_SIZE);
    dirPartOffset = offset;

    while (1)
    {
	if (dirPartOffset == 0)
	{
	    /* this is an error, but silently fail */
	    break;
	}
	read_dir_node(fd, dir, dirPartOffset);
	for (i = 0; i < dir->num_ent_used; i++)
	{
	    ocfs_file_entry *fe;
	    int off = 512;	/* move past the dirnode header */

	    off += (512 * dir->index[i]);
	    fe = (ocfs_file_entry *) (((void *) dir) + off);

#ifdef LIBDEBUGOCFS
            handle_one_file_entry(fd, fe, buf);
#else
            handle_one_file_entry(fd, fe, (void *)parent);
#endif
	}
	/* is there another directory chained off of this one? */
	if (dir->next_node_ptr == -1)
	    break;		// nope, we're done
	else
	    dirPartOffset = dir->next_node_ptr;	// keep going

    }
    free(dir);
}

void get_file_data_mapping(ocfs_super * vcb, ocfs_file_entry * fe,
			   filedata * data)
{
    int status;
    ocfs_inode fcb;
    __s64 lbo;
    __s64 vbo = 0;

    if (!data)
	return;

    data->num = 0;
    data->array = NULL;
    data->linkname = NULL;
    data->user = fe->uid;
    data->group = fe->gid;
    data->major = data->minor = 0;
    data->mode = fe->prot_bits;
    switch (fe->attribs & (~OCFS_ATTRIB_FILE_CDSL))
    {
	case OCFS_ATTRIB_DIRECTORY:
	    data->mode |= S_IFDIR;
	    break;
	case OCFS_ATTRIB_CHAR:
	    data->mode |= S_IFCHR;
	    data->major = fe->dev_major;
	    data->minor = fe->dev_minor;
	    break;
	case OCFS_ATTRIB_BLOCK:
	    data->mode |= S_IFBLK;
	    data->major = fe->dev_major;
	    data->minor = fe->dev_minor;
	    break;
	case OCFS_ATTRIB_FIFO:
	    data->mode |= S_IFIFO;
	    break;
	case OCFS_ATTRIB_SYMLINK:
	    data->linkname = (char *) calloc(fe->file_size + 1, 1);
	    status =
		ocfs_read_disk(vcb, data->linkname, fe->file_size,
			     fe->extents[0].disk_off);
	    data->mode |= S_IFLNK;
	    break;
	case OCFS_ATTRIB_SOCKET:
	    data->mode |= S_IFSOCK;
	    break;
	default:		// if 0 after &ing with ~OCFS_ATTRIB_FILE_CDSL, it's a file
	case OCFS_ATTRIB_REG:
	    if (fe->attribs & OCFS_ATTRIB_FILE_CDSL)
	    {
		// do the cdsl sleight-of-hand and overwrite the file entry
		__u64 offsets[MAX_NODES];

		ocfs_read_disk(vcb, offsets, (MAX_NODES * sizeof(__u64)),
			     fe->extents[0].disk_off);
		ocfs_read_disk(vcb, fe, OCFS_SECTOR_SIZE,
			     offsets[vcb->node_num]);
	    }
	    data->mode |= S_IFREG;
	    fcb.alloc_size = 0;
	    fcb.file_disk_off = fe->this_sector;
	    ocfs_extent_map_init(&(fcb.map));
	    status =
		ocfs_lookup_file_allocation(vcb, &fcb, vbo, &lbo, fe->file_size,
					 &data->num, (void *) &data->array);
	    ocfs_extent_map_destroy(&(fcb.map));
	    break;
    }

}

#ifndef LIBDEBUGOCFS
void traverse_extent(ocfs_super * vcb, ocfs_extent_group * exthdr, int flag)
{
	ocfs_extent_group *ext;
	int i, fd;
	bool prev_ptr_error;

	if ((ext = malloc_aligned(512)) == NULL) {
		printf("error: out of memory\n");
		return ;
	}

	fd = (int) vcb->sb->s_dev;

	for (i = 0; i < exthdr->next_free_ext; ++i) {
		if (!exthdr->extents[i].disk_off)
			continue;

		read_extent(fd, ext, exthdr->extents[i].disk_off);

		if (exthdr->this_ext != ext->up_hdr_node_ptr)
			prev_ptr_error = true;
		else
			prev_ptr_error = false;

		if (flag == EXTENT_HEADER) {
			if (!IS_VALID_EXTENT_HEADER (ext)) {
				printf("\tInvalid extent header\n\n");
				continue;
			}
		} else {
			if (!IS_VALID_EXTENT_DATA (ext)) {
				printf("\tInvalid extent data\n\n");
				continue;
			}
		}

		print_extent(ext, args.twoFourbyte, prev_ptr_error);

		if (flag == EXTENT_HEADER) {
			if (ext->granularity)
				traverse_extent(vcb, ext, EXTENT_HEADER);
			else
				traverse_extent(vcb, ext, EXTENT_DATA);
		}
	}

	free(ext);

    return ;
}

void traverse_fe_extents(ocfs_super * vcb, ocfs_file_entry *fe)
{
	int i, fd;
	ocfs_extent_group *ext;
	int type;
	bool prev_ptr_error;

	if ((ext = malloc_aligned(512)) == NULL) {
		printf("error: out of memory\n");
		return ;
	}

	fd = (int) vcb->sb->s_dev;
    
	for (i = 0; i < fe->next_free_ext; i++) {
		if (!fe->extents[i].disk_off)
			continue;

		read_extent(fd, ext, fe->extents[i].disk_off);

		if (fe->this_sector != ext->up_hdr_node_ptr)
			prev_ptr_error = true;
		else
			prev_ptr_error = false;

		if (fe->granularity) {
			if (!IS_VALID_EXTENT_HEADER (ext)) {
				printf("\tInvalid extent header\n\n");
				continue;
			}

 			print_extent(ext, args.twoFourbyte, prev_ptr_error);
			type = ext->granularity ? EXTENT_HEADER : EXTENT_DATA;
			traverse_extent(vcb, ext, type);
		} else {
			if (!IS_VALID_EXTENT_DATA (ext)) {
				printf("\tInvalid extent data\n\n");
				continue;
			}
 			print_extent(ext, args.twoFourbyte, prev_ptr_error);
		}
	}

	free(ext);
}
#endif

/*
 * for LIBDEBUGOCFS: returns the dir offset for FIND_MODE_DIR 
 *                   or the parent dir offset for FIND_MODE_FILE
 * for executable:   finds and prints either the ocfs_dir_node or
 *                   ocfs_file_entry structure, depending upon mode
 */
void find_file_entry(ocfs_super * vcb, __u64 offset, const char *parent,
		     const char *searchFor, int mode, void *buf)
{
    ocfs_dir_node *dir, *foundDir;
    int i, fd;
    char *newname;
    __u64 ret = 0, dirPartOffset;

    fd = (int) vcb->sb->s_dev;
    dir = (ocfs_dir_node *) malloc_aligned(DIR_NODE_SIZE);
    dirPartOffset = offset;

    while (1)
    {
	if (dirPartOffset == 0)
	{
	    /* this is an error, but silently fail */
	    break;
	}
	read_dir_node(fd, dir, dirPartOffset);
	for (i = 0; i < dir->num_ent_used; i++)
	{
	    ocfs_file_entry *fe;
	    int off = 512;	/* move past the dirnode header */

	    off += (512 * dir->index[i]);
	    fe = (ocfs_file_entry *) (((void *) dir) + off);

	    if (!fe->sync_flags || (fe->sync_flags & DELETED_FLAGS))
		    continue;

	    newname =
		(char *) malloc_aligned(strlen(parent) + strlen(fe->filename) + 2);

	    if (fe->attribs & OCFS_ATTRIB_DIRECTORY)
	    {
		sprintf(newname, "%s%s/", parent, fe->filename);
		if (strcmp(searchFor, newname) == 0)
		{
		    if (mode == FIND_MODE_FILE || mode == FIND_MODE_FILE_EXTENT)
			ret = offset;	// return the first part of the dir chain
		    else if (mode == FIND_MODE_DIR)
			ret = fe->extents[0].disk_off;
		    else if (mode == FIND_MODE_FILE_DATA)
			get_file_data_mapping(vcb, fe, buf);

#ifndef LIBDEBUGOCFS
		    printf("\tName = %s\n", newname);
		    if (mode == FIND_MODE_FILE || mode == FIND_MODE_FILE_EXTENT)
		    {
			print_file_entry(fe);
			if (!fe->local_ext && mode == FIND_MODE_FILE_EXTENT)
				traverse_fe_extents(vcb, fe);
		    }
		    else if (mode == FIND_MODE_DIR)
		    {
			__u64 dir_off = fe->extents[0].disk_off;
			foundDir = (ocfs_dir_node *) malloc_aligned(DIR_NODE_SIZE);

			while(1) {
				read_dir_node(fd, foundDir, dir_off);
				print_dir_node(foundDir);
				if (!args.showDirentAll ||
				    foundDir->next_node_ptr == INVALID_NODE_POINTER)
					break;
				dir_off = foundDir->next_node_ptr;
				memset(foundDir, 0, DIR_NODE_SIZE);
				printf("dirinfo:\n");
			}
			free(foundDir);
		    }
#endif
		    free(newname);
		    break;
		}
		else if (strstr(searchFor, newname) == searchFor)
		{
		    find_file_entry(vcb, fe->extents[0].disk_off, newname,
				    searchFor, mode, (void *) (&ret));
		    free(newname);
		    break;
		}
	    }
	    else		/* not a directory */
	    {
		sprintf(newname, "%s%s", parent, fe->filename);
		if (strcmp(searchFor, newname) == 0)
		{
		    if (mode == FIND_MODE_FILE || mode == FIND_MODE_FILE_EXTENT)
			ret = offset;	// return the first part of the dir chain
		    else if (mode == FIND_MODE_DIR)
			ret = 0;	// file found, not dir 
		    else if (mode == FIND_MODE_FILE_DATA)
			get_file_data_mapping(vcb, fe, buf);

#ifndef LIBDEBUGOCFS
		    if (mode == FIND_MODE_DIR)
		    {
			printf("found a file named %s, not a directory.\n",
			       newname);
			exit(1);
		    }
		    printf("\tName = %s\n", newname);
		    print_file_entry(fe);
		    if (!fe->local_ext && mode == FIND_MODE_FILE_EXTENT)
			    traverse_fe_extents(vcb, fe);
#endif
		    free(newname);
		    break;
		}
	    }
	    free(newname);
	}
	/* is there another directory chained off of this one? */
	if (dir->next_node_ptr == -1)
	    break;		// nope, we're done
	else
	    dirPartOffset = dir->next_node_ptr;	// keep going
    }

    free(dir);
    if ((mode == FIND_MODE_FILE || mode == FIND_MODE_DIR ||
	 mode == FIND_MODE_FILE_EXTENT) && buf)
	*(__u64 *) buf = ret;
}


int suck_file(ocfs_super * vcb, const char *path, const char *file)
{
    int fd, newfd, mode, i, ret = 0;
    filedata data;
    mode_t oldmask;
    ocfs_io_runs *run;

    fd = (int) vcb->sb->s_dev;
    oldmask = umask(0000);

    if (unlink(file) == -1)
    {
#ifndef LIBDEBUGOCFS
	printf("failed to unlink file: %s\n", file);
#endif
    }

    if (access(file, F_OK) == -1)
    {
	find_file_entry(vcb, vcb->vol_layout.root_start_off, "/",
			path, FIND_MODE_FILE_DATA, &data);

	if (S_ISLNK(data.mode) && data.linkname)
	{
	    newfd = symlink(data.linkname, file);
	    free(data.linkname);
	}
	else if (S_ISFIFO(data.mode) || S_ISCHR(data.mode) ||
		 S_ISBLK(data.mode))
	{
	    if (S_ISFIFO(data.mode))
		data.major = data.minor = 0;
	    newfd = mknod(file, data.mode, makedev(data.major, data.minor));
	}
	else if (S_ISSOCK(data.mode))
	{
	    // unimplemented...
	}
	else if (S_ISDIR(data.mode))
	    newfd = mkdir(file, data.mode);
	else if (S_ISREG(data.mode))
	{
	    void *filebuf = malloc_aligned(FILE_BUFFER_SIZE);
	    __u32 remaining, readlen;

	    newfd = creat(file, data.mode);
	    if (newfd != -1)
	    {
		for (i = 0; i < data.num; i++)
		{
		    run = (ocfs_io_runs *) & (data.array[i]);

		    // in new file: seek to run->Offset
		    // in ocfs: read from run->disk_off, run->byte_cnt bytes
		    remaining = run->byte_cnt;
		    myseek64(newfd, run->offset, SEEK_SET);
		    myseek64(fd, run->disk_off, SEEK_SET);
		    while (remaining > 0)
		    {
			readlen =
			    remaining <
			    FILE_BUFFER_SIZE ? remaining : FILE_BUFFER_SIZE;
			if (read(fd, filebuf, readlen) != readlen ||
			    write(newfd, filebuf, readlen) != readlen)
			{
			    ret = 2;
			    goto do_close;
			}
			remaining -= readlen;
		    }
		}
	    }
	}

      do_close:
	if (newfd != -1)
	    close(newfd);
	if (chown(file, data.user, data.group) == -1)
        {
#ifndef LIBDEBUGOCFS
	    printf("chown failed!\n");	// error
#endif
        }
    }
    else
    {
	ret = 1;
    }

  bail:
    umask(oldmask);
    return ret;
}
