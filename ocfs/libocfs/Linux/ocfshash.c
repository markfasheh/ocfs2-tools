/*
 * ocfshash.c
 *
 * Allows for creation and destruction of a hash table which one
 * can use to read, write and delete data.
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

#ifdef __KERNEL__
#include <ocfs.h>
#else
#include <libocfs.h>
#endif


/* Tracing */
#define OCFS_DEBUG_CONTEXT      OCFS_DEBUG_CONTEXT_HASH

/*
 * ocfs_hash_create()
 *
 */
int ocfs_hash_create (HASHTABLE *ht, __u32 noofbits)
{
	int ret = 0;
	__u32 size = 0;

	LOG_ENTRY ();

	if (noofbits > 32 || noofbits < 1) {
		LOG_ERROR_STR ("Error in noofbits");
		goto bail;
	}

	ht->size = hashsize (noofbits);
	ht->mask = hashmask (noofbits);
	ht->inithash = 0x10325476;
	ht->entries = 0;
	ht->newbuckets = 0;
	ht->reusedbuckets = 0;
	ht->freelist = NULL;
	ht->lastfree = NULL;

	ocfs_init_sem (&(ht->hashlock));

	size = ht->size * sizeof (HASHBUCKET);
	ht->buckets = (HASHBUCKET *) ocfs_malloc (size);
	if (!ht->buckets) {
		LOG_ERROR_ARGS ("unable to allocate %u bytes of memory", size);
		goto bail;
	}

	memset (ht->buckets, 0, (ht->size * sizeof (HASHBUCKET)));
	ret = 1;

      bail:
	LOG_EXIT_LONG (ret);
	return ret;
}				/* ocfs_hash_create */

/*
 * ocfs_hash_destroy()
 *
 * @ht: ptr to the hash table
 * @freefn: if not null, uses function to free bucket->val
 *
 */
void ocfs_hash_destroy (HASHTABLE *ht, void (*freefn) (const void *p))
{
	HASHBUCKET *bucket;
	HASHBUCKET *nxtbucket;
	__u32 slot;

	LOG_ENTRY ();

	if (!ht || !ht->buckets)
		goto bail;

	if (ht->buckets) {
		for (slot = 0; slot < ht->size; slot++) {
			if (freefn) {
				bucket = &(ht->buckets[slot]);
				if (bucket->key && bucket->val)
					freefn (bucket->val);
			}

			bucket = ht->buckets[slot].next;
			while (bucket) {
				if (freefn && bucket->key && bucket->val)
					freefn (bucket->val);
				nxtbucket = bucket->next;
				ocfs_safefree (bucket);
				bucket = nxtbucket;
			}
		}
	}

	bucket = ht->freelist;
	while (bucket) {
		nxtbucket = bucket->next;
		ocfs_safefree (bucket);
		bucket = nxtbucket;
	}

	ocfs_safefree (ht->buckets);
	ht->buckets = NULL;

      bail:
	LOG_EXIT ();
	return;
}				/* ocfs_hash_destroy */

/*
 * ocfs_hash_add()
 *
 * @ht: ptr to the hash table
 * @key: key
 * @keylen: length of key
 * @val: value
 * @vallen: length of value
 *
 */
int ocfs_hash_add (HASHTABLE * ht, void *key, __u32 keylen, void *val, __u32 vallen,
		   void **found, __u32 *foundlen)
{
	HASHBUCKET *bucket;
	HASHBUCKET *prvbucket = NULL;
	HASHBUCKET *lastbucket;
	__u32 slot;
	int ret = 1;
	int lockacqrd = false;

	LOG_ENTRY ();

	if (!ht || !ht->buckets) {
		ret = 0;
		goto bail;
	}

	*found = NULL;
	*foundlen = 0;

	slot = hash (key, keylen, ht->inithash) & ht->mask;
	bucket = &(ht->buckets[slot]);

	/* Acquire Lock */
	ocfs_down_sem (&(ht->hashlock), true);
	lockacqrd = true;

	while (bucket) {
		if (bucket->key) {
			if (!memcmp (bucket->key, key, keylen)) {
				/* return warning & val if key already exists */
				LOG_TRACE_STR ("Duplicate key");
				*found = bucket->val;
				*foundlen = bucket->vallen;
				ret = 2;
				goto bail;
			}
		} else {
			/* Fill the empty bucket */
			bucket->key = key;
			bucket->keylen = keylen;
			bucket->val = val;
			bucket->vallen = vallen;

			/* Increment the number of entries */
			ht->entries++;
			ret = 1;
			goto bail;
		}
		prvbucket = bucket;
		bucket = bucket->next;
	}

	/* Save the last bucket for this slot */
	lastbucket = prvbucket;

	/* Check if any bucket in freelist ... */
	if (ht->freelist) {
		/* ... if so, attach it to the end of the slot list ... */
		lastbucket->next = bucket = ht->freelist;

		/* ... and detach it from the freelist */
		if (ht->lastfree == ht->freelist)
			ht->freelist = ht->lastfree = NULL;
		else
			ht->freelist = ht->freelist->next;

		/* Fill the empty bucket */
		bucket->key = key;
		bucket->keylen = keylen;
		bucket->val = val;
		bucket->vallen = vallen;
		bucket->next = NULL;
		ht->reusedbuckets++;

		/* Increment the number of entries */
		ht->entries++;
		ret = 1;
		goto bail;
	}

	/* Create a new bucket and add to the end of list */
	if ((bucket = (HASHBUCKET *) ocfs_malloc (sizeof (HASHBUCKET))) == NULL) {
		LOG_ERROR_ARGS ("unable to allocate %u bytes of memory",
				sizeof (HASHBUCKET));
		ret = 0;
		goto bail;
	}

	bucket->key = key;
	bucket->keylen = keylen;
	bucket->val = val;
	bucket->vallen = vallen;
	bucket->next = NULL;
	lastbucket->next = bucket;
	ht->newbuckets++;

	/* Increment the number of entries */
	ht->entries++;

      bail:
	/* Release Lock */
	if (lockacqrd)
		ocfs_up_sem (&(ht->hashlock));

	LOG_EXIT_LONG (ret);
	return ret;
}				/* ocfs_hash_add */

/*
 * ocfs_hash_del()
 *
 * @ht: ptr to hash table
 * @key: key to be deleted
 * @keylen: length of key
 *
 */
int ocfs_hash_del (HASHTABLE * ht, void *key, __u32 keylen)
{
	HASHBUCKET *bucket;
	HASHBUCKET *prvbucket = NULL;
	__u32 slot;
	int ret = 0;
	int lockacqrd = false;

	LOG_ENTRY ();

	if (!ht || !ht->buckets)
		goto bail;

	slot = hash (key, keylen, ht->inithash) & ht->mask;
	bucket = &(ht->buckets[slot]);

	/* Acquire Lock */
	ocfs_down_sem (&(ht->hashlock), true);
	lockacqrd = true;

	while (bucket) {
		if (bucket->key) {
			if (!memcmp (bucket->key, key, keylen)) {
				/* Found it */
				if (!prvbucket) {
					/* If first bucket, clear it */
					bucket->key = NULL;
				} else {
					/* If not first bucket, detach the bucket from list ... */
					prvbucket->next = bucket->next;

					/* ... clear it ... */
					bucket->key = NULL;
					bucket->next = NULL;

					/* ... and attach to the end of the free list */
					if (ht->lastfree) {
						ht->lastfree->next = bucket;
						ht->lastfree = bucket;
					} else {
						ht->lastfree = ht->freelist =
						    bucket;
					}
				}
				/* Decrement the number of entries and exit */
				ht->entries--;
				ret = 1;
				goto bail;
			}
		}
		prvbucket = bucket;
		bucket = bucket->next;
	}

      bail:
	/* Release Lock */
	if (lockacqrd)
		ocfs_up_sem (&(ht->hashlock));

	LOG_EXIT_LONG (ret);
	return ret;
}				/* ocfs_hash_del */

/*
 * ocfs_hash_get()
 *
 */
int ocfs_hash_get (HASHTABLE * ht, void *key, __u32 keylen, void **val, __u32 * vallen)
{
	HASHBUCKET *bucket;
	__u32 slot;
	int ret = 0;
	int lockacqrd = false;

	LOG_ENTRY ();

	if (!ht || !ht->buckets)
		goto bail;

	slot = hash (key, keylen, ht->inithash) & ht->mask;
	bucket = &(ht->buckets[slot]);

	/* Acquire Lock */
	ocfs_down_sem (&(ht->hashlock), true);
	lockacqrd = true;

	while (bucket) {
		if (bucket->key) {
			if (!memcmp (bucket->key, key, keylen)) {
				/* found it */
				*val = bucket->val;
				*vallen = bucket->vallen;
				ret = 1;
				goto bail;
			}
		}
		bucket = bucket->next;
	}

      bail:
	/* Release Lock */
	if (lockacqrd)
		ocfs_up_sem (&(ht->hashlock));

	LOG_EXIT_LONG (ret);
	return ret;
}				/* ocfs_hash_get */

/*
 * ocfs_hash_stat()
 *
 */
void ocfs_hash_stat (HASHTABLE * ht, char *data, __u32 datalen)
{
	HASHBUCKET *bucket;
	__u32 slot;
	__u32 i;
	char *p;
	__u32 len = 0;
	__u32 rlen;
	__u32 stats[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	int lockacqrd = false;

	LOG_ENTRY ();

	if (!ht || !ht->buckets)
		goto bail;

	if (!data || !datalen)
		goto bail;

	/* Acquire Lock */
	ocfs_down_sem (&(ht->hashlock), true);
	lockacqrd = true;

	for (slot = 0; slot < ht->size; ++slot) {
		bucket = &(ht->buckets[slot]);
		i = 0;

		while (bucket) {
			if (bucket->key)
				++i;
			bucket = bucket->next;
		}

		if (i < 9)
			stats[i]++;
		else
			stats[9]++;
	}

	for (i = 0, p = data, rlen = datalen; i < 10; ++i, p += len, rlen -= len)
		len = snprintf (p, rlen, "%2u: %u\n", i, stats[i]);

	len = rlen;
	len += snprintf (p, rlen, "New: %u, Reused: %u\n", ht->newbuckets,
			 ht->reusedbuckets);

	data[len + 1] = '\0';

      bail:
	/* Release Lock */
	if (lockacqrd)
		ocfs_up_sem (&(ht->hashlock));

	LOG_EXIT ();
	return;
}				/* ocfs_hash_stat */

/*
 * --------------------------------------------------------------------
 * hash() -- hash a variable-length key into a 32-bit value
 *   k       : the key (the unaligned variable-length array of bytes)
 *   len     : the length of the key, counting by bytes
 *   initval : can be any 4-byte value
 *
 * Returns a 32-bit value.  Every bit of the key affects every bit of
 * the return value.  Every 1-bit and 2-bit delta achieves avalanche.
 * About 6*len+35 instructions.
 *
 * The best hash table sizes are powers of 2.  There is no need to do
 * mod a prime (mod is sooo slow!).  If you need less than 32 bits,
 * use a bitmask.  For example, if you need only 10 bits, do
 * h = (h & hashmask(10));
 * In which case, the hash table should have hashsize(10) elements.
 *
 * If you are hashing n strings (__u8 **)k, do it like this:
 * for (i=0, h=0; i<n; ++i) h = hash( k[i], len[i], h);
 *
 * By Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.  You may use this
 * code any way you wish, private, educational, or commercial.  It's free.
 *
 * See http://burtleburtle.net/bob/hash/evahash.html
 * Use for hash table lookup, or anything where one collision in 2^^32 is
 * acceptable.  Do NOT use for cryptographic purposes.
 * --------------------------------------------------------------------
 */
__u32 hash (k, length, initval)
register __u8 *k;		/* the key */
register __u32 length;		/* the length of the key */
register __u32 initval;		/* the previous hash, or an arbitrary value */
{
	register __u32 a, b, c, len;

	/* Set up the internal state */
	len = length;
	a = b = 0x9e3779b9;	/* the golden ratio; an arbitrary value */
	c = initval;		/* the previous hash value */

   /*---------------------------------------- handle most of the key */
	while (len >= 12) {
		a += (k[0] + ((__u32) k[1] << 8) + ((__u32) k[2] << 16) +
		      ((__u32) k[3] << 24));
		b += (k[4] + ((__u32) k[5] << 8) + ((__u32) k[6] << 16) +
		      ((__u32) k[7] << 24));
		c += (k[8] + ((__u32) k[9] << 8) + ((__u32) k[10] << 16) +
		      ((__u32) k[11] << 24));
		mix (a, b, c);
		k += 12;
		len -= 12;
	}

   /*------------------------------------- handle the last 11 bytes */
	c += length;
	switch (len) {		/* all the case statements fall through */
	    case 11:
		    c += ((__u32) k[10] << 24);
	    case 10:
		    c += ((__u32) k[9] << 16);
	    case 9:
		    c += ((__u32) k[8] << 8);
		    /* the first byte of c is reserved for the length */
	    case 8:
		    b += ((__u32) k[7] << 24);
	    case 7:
		    b += ((__u32) k[6] << 16);
	    case 6:
		    b += ((__u32) k[5] << 8);
	    case 5:
		    b += k[4];
	    case 4:
		    a += ((__u32) k[3] << 24);
	    case 3:
		    a += ((__u32) k[2] << 16);
	    case 2:
		    a += ((__u32) k[1] << 8);
	    case 1:
		    a += k[0];
		    /* case 0: nothing left to add */
	}
	mix (a, b, c);
   /*-------------------------------------------- report the result */
	return c;
}				/* hash */
