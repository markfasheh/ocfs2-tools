/*
 * ocfsgenmisc.h
 *
 * Function prototypes for related 'C' file.
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
 * Authors: Neeraj Goyal, Suchit Kaura, Kurt Hackel, Sunil Mushran,
 *          Manish Singh, Wim Coekaerts
 */

#ifndef _OCFSGENMISC_H_
#define _OCFSGENMISC_H_

int ocfs_create_meta_log_files (ocfs_super * osb);

int ocfs_create_new_oin (ocfs_inode ** Returnedoin,
		  __u64 * AllocationSize,
		  __u64 * EndOfFile, struct file *FileObject, ocfs_super * osb);

int ocfs_create_root_dir_node (ocfs_super * osb);

int ocfs_create_root_oin (ocfs_super * osb);

void ocfs_delete_all_extent_maps (ocfs_inode * oin);

void ocfs_release_oin (ocfs_inode * oin, bool need_lock);

int ocfs_initialize_osb (ocfs_super * osb,
		   ocfs_vol_disk_hdr * VolDiskHdr,
		   ocfs_vol_label * VolLabel, __u32 SectorSize);

int ocfs_verify_volume (ocfs_vol_disk_hdr * VolDiskHdr);

int ocfs_vol_member_reconfig (ocfs_super * osb);

int ocfs_check_volume (ocfs_super * osb);

void ocfs_delete_osb (ocfs_super * osb);

int ocfs_commit_cache (ocfs_super * osb, bool Flag);

int ocfs_is_dir_empty (ocfs_super * osb, ocfs_dir_node * dirnode, bool * empty);

/* sorry about all the macros, but file and line are important */

#ifndef USERSPACE_TOOL
/* lockres macros */
#ifdef OCFS_MEM_DBG
#define ocfs_allocate_lockres()						\
({									\
	ocfs_lock_res *_l = NULL;					\
	 _l = (ocfs_lock_res *)ocfs_dbg_slab_alloc 			\
	 	(OcfsGlobalCtxt.lockres_cache, __FILE__, __LINE__);	\
	if (_l)	{							\
       		memset (_l, 0, sizeof(ocfs_lock_res));			\
		atomic_inc (&(OcfsGlobalCtxt.cnt_lockres));		\
	}								\
	_l;								\
})

#define ocfs_free_lockres(_r)						\
do {									\
	ocfs_dbg_slab_free (OcfsGlobalCtxt.lockres_cache, _r);		\
	atomic_dec (&(OcfsGlobalCtxt.cnt_lockres));			\
} while (0)

#else  /* !OCFS_MEM_DBG */
#define ocfs_allocate_lockres()						\
({									\
 	ocfs_lock_res *_l = NULL;					\
	_l = (ocfs_lock_res *)kmem_cache_alloc				\
			(OcfsGlobalCtxt.lockres_cache, GFP_NOFS);	\
	if (_l)	{							\
       		memset (_l, 0, sizeof(ocfs_lock_res));			\
		atomic_inc (&(OcfsGlobalCtxt.cnt_lockres));		\
	}								\
	_l;								\
})

#define ocfs_free_lockres(_r)						\
do {									\
	kmem_cache_free (OcfsGlobalCtxt.lockres_cache, _r);		\
	atomic_dec (&(OcfsGlobalCtxt.cnt_lockres));			\
} while (0)

#endif

#define _ocfs_get_lockres(_r)					\
do {								\
	if (_r) 						\
		atomic_inc(&((_r)->lr_ref_cnt));		\
} while (0)

#define _ocfs_put_lockres(_r)					\
do {								\
	if (_r) {						\
		if (atomic_dec_and_test(&((_r)->lr_ref_cnt))) 	\
			ocfs_free_lockres(_r);			\
	}							\
} while (0)

#ifdef OCFS_DBG_LOCKRES
#define ocfs_get_lockres(_r)						\
do {									\
	if (_r) {							\
		if (debug_level & OCFS_DEBUG_LEVEL_LOCKRES)		\
			printk("(%d) get: 0x%08x, %d, %s, %d\n",	\
			       ocfs_getpid(), (_r),			\
			       atomic_read(&((_r)->lr_ref_cnt)) + 1,	\
			       __FUNCTION__, __LINE__);			\
		_ocfs_get_lockres(_r);					\
	} else {							\
		if (debug_level & OCFS_DEBUG_LEVEL_LOCKRES)		\
			printk("(%d) get: null, -1, %s, %d\n",		\
			       ocfs_getpid(), __FUNCTION__, __LINE__);	\
	}								\
} while (0)

#define ocfs_put_lockres(_r)						\
do {									\
	if (_r) {							\
		if (debug_level & OCFS_DEBUG_LEVEL_LOCKRES)		\
			printk("(%d) put: 0x%08x, %d, %s, %d\n",	\
			       ocfs_getpid(), (_r),			\
			       atomic_read(&((_r)->lr_ref_cnt)) - 1,	\
			       __FUNCTION__, __LINE__);			\
		_ocfs_put_lockres(_r);					\
	} else {							\
		if (debug_level & OCFS_DEBUG_LEVEL_LOCKRES)		\
			printk("(%d) put: null, -1, %s, %d\n",		\
			       ocfs_getpid(), __FUNCTION__, __LINE__);	\
	}								\
} while (0)
#else	/* !OCFS_DBG_LOCKRES */
#define ocfs_get_lockres(_r)		_ocfs_get_lockres(_r)
#define	ocfs_put_lockres(_r)		_ocfs_put_lockres(_r)
#endif



/* ofile macros */
#ifdef OCFS_MEM_DBG
#define ocfs_allocate_ofile()    ((ocfs_file *)({ \
        ocfs_file *of = NULL; \
        of = ocfs_dbg_slab_alloc(OcfsGlobalCtxt.ofile_cache, __FILE__, __LINE__); \
	if (of != NULL) { \
	  memset (of, 0, sizeof (ocfs_file)); \
	  of->obj_id.type = OCFS_TYPE_OFILE; \
          of->obj_id.size = sizeof (ocfs_file); \
        } \
	of; }))

#define ocfs_release_ofile(of)						\
do {									\
	if (of) {							\
        	ocfs_release_dirnode ((of)->curr_dir_buf);		\
        	ocfs_dbg_slab_free (OcfsGlobalCtxt.ofile_cache, (of));	\
	}								\
} while (0)
#else  /* !OCFS_MEM_DBG */
#define ocfs_allocate_ofile()    ((ocfs_file *)({ \
        ocfs_file *of = NULL; \
	of = kmem_cache_alloc (OcfsGlobalCtxt.ofile_cache, GFP_NOFS); \
	if (of != NULL) { \
	  memset (of, 0, sizeof (ocfs_file)); \
	  of->obj_id.type = OCFS_TYPE_OFILE; \
          of->obj_id.size = sizeof (ocfs_file); \
        } \
	of; }))

#define ocfs_release_ofile(of)						\
do {									\
	if (of) {							\
        	ocfs_release_dirnode ((of)->curr_dir_buf);		\
		kmem_cache_free (OcfsGlobalCtxt.ofile_cache, (of));	\
	}								\
} while (0)
#endif


/* file entry macros */
#ifdef OCFS_MEM_DBG
#define ocfs_allocate_file_entry()  ((ocfs_file_entry *)({ \
	ocfs_file_entry *FileEntry = NULL; \
	FileEntry = ocfs_dbg_slab_alloc (OcfsGlobalCtxt.fe_cache, __FILE__, __LINE__); \
	if (FileEntry != NULL) \
	  memset (FileEntry, 0, OCFS_SECTOR_SIZE); \
	FileEntry; }))

#define ocfs_release_file_entry(fe)					  \
	do {								  \
		if (fe) {						  \
			ocfs_dbg_slab_free (OcfsGlobalCtxt.fe_cache, fe); \
			fe = NULL;					  \
		}							  \
	} while (0)
#else  /* !OCFS_MEM_DBG */
#define ocfs_allocate_file_entry()  ((ocfs_file_entry *)({ \
	ocfs_file_entry *FileEntry = NULL; \
	FileEntry = kmem_cache_alloc (OcfsGlobalCtxt.fe_cache, GFP_NOFS); \
	if (FileEntry != NULL) \
 	  memset (FileEntry, 0, OCFS_SECTOR_SIZE); \
	FileEntry; }))

#define ocfs_release_file_entry(fe)					\
	do {								\
		if (fe) {						\
			kmem_cache_free (OcfsGlobalCtxt.fe_cache, fe);	\
			fe = NULL;					\
		}							\
	} while (0)
#endif

/* oin macros - currently the release is handled separately */
#ifdef OCFS_MEM_DBG
#define ocfs_allocate_oin()  ((ocfs_inode *)({ \
	ocfs_inode *oin = NULL; \
	oin = ocfs_dbg_slab_alloc (OcfsGlobalCtxt.oin_cache, __FILE__, __LINE__); \
	if (oin != NULL) { \
 	  memset (oin, 0, sizeof (ocfs_inode)); \
	  oin->obj_id.type = OCFS_TYPE_OIN; \
          oin->obj_id.size = sizeof (ocfs_inode); \
        } \
	oin; })) 
#else  /* !OCFS_MEM_DBG */
#define ocfs_allocate_oin()  ((ocfs_inode *)({ \
	ocfs_inode *oin = NULL; \
	oin = kmem_cache_alloc (OcfsGlobalCtxt.oin_cache, GFP_NOFS); \
	if (oin != NULL) { \
          memset (oin, 0, sizeof (ocfs_inode)); \
          oin->obj_id.type = OCFS_TYPE_OIN; \
          oin->obj_id.size = sizeof (ocfs_inode); \
        } \
	oin; })) 
#endif

/* dirnode macros */
#ifdef OCFS_MEM_DBG
#define ocfs_allocate_dirnode()					\
((ocfs_dir_node *)({						\
	ocfs_dir_node *_dn = NULL;				\
	_dn = ocfs_dbg_slab_alloc (OcfsGlobalCtxt.dirnode_cache, __FILE__, __LINE__); \
	if (_dn)						\
		memset (_dn, 0, OCFS_DEFAULT_DIR_NODE_SIZE);	\
	_dn; }))

#define ocfs_release_dirnode(_dn)				\
	do {							\
		if (_dn) {					\
			ocfs_dbg_slab_free (OcfsGlobalCtxt.dirnode_cache, (_dn));\
			(_dn) = NULL;				\
		}						\
	} while (0)
#else  /* !OCFS_MEM_DBG */
#define ocfs_allocate_dirnode()					\
((ocfs_dir_node *)({						\
	ocfs_dir_node *_dn = NULL;				\
	_dn = kmem_cache_alloc (OcfsGlobalCtxt.dirnode_cache, GFP_NOFS);\
	if (_dn)						\
		memset (_dn, 0, OCFS_DEFAULT_DIR_NODE_SIZE);	\
	_dn; }))

#define ocfs_release_dirnode(_dn)				\
	do {							\
		if (_dn) {					\
			kmem_cache_free (OcfsGlobalCtxt.dirnode_cache, (_dn));\
			(_dn) = NULL;				\
		}						\
	} while (0)
#endif
#endif  /* !USERSPACE_TOOL */




/* userspace stuff */
#ifdef USERSPACE_TOOL
/* lockres macros */
#define ocfs_allocate_lockres()	((ocfs_lock_res *)ocfs_malloc(sizeof(ocfs_lock_res)))
#define ocfs_free_lockres(_r)	ocfs_safefree(_r)

#define ocfs_get_lockres(_r)					\
do {								\
	if (_r) 						\
		atomic_inc(&((_r)->lr_ref_cnt));		\
} while (0)

#define ocfs_put_lockres(_r)					\
do {								\
	if (_r) {						\
		if (atomic_dec_and_test(&((_r)->lr_ref_cnt))) 	\
			ocfs_free_lockres(_r);			\
	}							\
} while (0)

/* ofile macros */
#define ocfs_allocate_ofile()    ((ocfs_file *)({ \
        ocfs_file *of = NULL; \
	of = (ocfs_file *)ocfs_malloc(sizeof(ocfs_file)); \
	if (of != NULL) { \
	  memset (of, 0, sizeof (ocfs_file)); \
	  of->obj_id.type = OCFS_TYPE_OFILE; \
          of->obj_id.size = sizeof (ocfs_file); \
        } \
	of; }))

#define ocfs_release_ofile(of) ({ \
	ocfs_safefree (of->curr_dir_buf); \
	ocfs_safefree (of);  })


/* file entry macros */
#define ocfs_allocate_file_entry()  ((ocfs_file_entry *)({ \
	ocfs_file_entry *FileEntry = NULL; \
	FileEntry = (ocfs_file_entry *)ocfs_malloc(OCFS_SECTOR_SIZE); \
	if (FileEntry != NULL) \
 	  memset (FileEntry, 0, OCFS_SECTOR_SIZE); \
	FileEntry; }))

#define ocfs_release_file_entry(fe)		 ocfs_safefree(fe)

/* oin macros */
#define ocfs_allocate_oin()  ((ocfs_inode *)({ \
	ocfs_inode *oin = NULL; \
	oin = (ocfs_inode *)ocfs_malloc(sizeof(ocfs_inode)); \
	if (oin != NULL) { \
          memset (oin, 0, sizeof (ocfs_inode)); \
          oin->obj_id.type = OCFS_TYPE_OIN; \
          oin->obj_id.size = sizeof (ocfs_inode); \
        } \
	oin; })) 

#define ocfs_release_oin(oin,x)	({ \
	if (x) \
		ocfs_safefree(oin);  })

/* dirnode macros */
#define ocfs_allocate_dirnode()					\
((ocfs_dir_node *)({						\
	ocfs_dir_node *_dn = NULL;				\
	_dn = (ocfs_dir_node *)ocfs_malloc(OCFS_DEFAULT_DIR_NODE_SIZE);\
	if (_dn)						\
		memset (_dn, 0, OCFS_DEFAULT_DIR_NODE_SIZE);	\
	_dn; }))

#define ocfs_release_dirnode(_dn)		ocfs_safefree(_dn)

#endif  /* USERSPACE_TOOL */

#endif				/* _OCFSGENMISC_H_ */
