/*
 * ocfshash.h
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

#ifndef  _OCFSHASH_H_
#define  _OCFSHASH_H_

/* Data structures */
typedef struct _HASHBUCKET
{
	void *key;
	__u32 keylen;
	void *val;
	__u32 vallen;
	struct _HASHBUCKET *next;
}
HASHBUCKET;

typedef struct
{
	__u32 size;
	__u32 mask;
	__u32 entries;
	__u32 inithash;
	__u32 newbuckets;		/* Used for statistics */
	__u32 reusedbuckets;	/* Used for statistics */
	ocfs_sem hashlock;
	HASHBUCKET *lastfree;
	HASHBUCKET *freelist;
	HASHBUCKET *buckets;
}
HASHTABLE;

/* Function prototypes */
int ocfs_hash_create (HASHTABLE * ht, __u32 noofbits);

void ocfs_hash_destroy (HASHTABLE * ht, void (*freefn) (const void *p));

int ocfs_hash_add (HASHTABLE * ht, void *key, __u32 keylen, void *val, __u32 vallen,
		   void **found, __u32 *foundlen);

int ocfs_hash_del (HASHTABLE * ht, void *key, __u32 keylen);

int ocfs_hash_get (HASHTABLE * ht, void *key, __u32 keylen, void **val, __u32 * vallen);

void ocfs_hash_stat (HASHTABLE * ht, char *data, __u32 datalen);

#define hashsize(n)             ((__u32)1<<(n))
#define hashmask(n)             (hashsize(n)-1)

#define HASHTABLE_DESTROYED(h)  (((HASHTABLE *)h)->buckets==NULL)

/*
 * --------------------------------------------------------------------
 * mix -- mix 3 32-bit values reversibly.
 * For every delta with one or two bits set, and the deltas of all three
 * high bits or all three low bits, whether the original value of a,b,c
 * is almost all zero or is uniformly distributed.
 * If mix() is run forward or backward, at least 32 bits in a,b,c
 * have at least 1/4 probability of changing.
 * If mix() is run forward, every bit of c will change between 1/3 and
 * 2/3 of the time.  (Well, 22/100 and 78/100 for some 2-bit deltas.)
 * mix() takes 36 machine instructions, but only 18 cycles on a superscalar
 * machine (like a Pentium or a Sparc).  No faster mixer seems to work,
 * that's the result of my brute-force search.  There were about 2^^68
 * hashes to choose from.  I only tested about a billion of those.
 * --------------------------------------------------------------------
 * */
#define mix(a,b,c)              \
{                               \
  a -= b; a -= c; a ^= (c>>13); \
  b -= c; b -= a; b ^= (a<<8);  \
  c -= a; c -= b; c ^= (b>>13); \
  a -= b; a -= c; a ^= (c>>12); \
  b -= c; b -= a; b ^= (a<<16); \
  c -= a; c -= b; c ^= (b>>5);  \
  a -= b; a -= c; a ^= (c>>3);  \
  b -= c; b -= a; b ^= (a<<10); \
  c -= a; c -= b; c ^= (b>>15); \
}

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
 * */
__u32 hash (__u8 * k, __u32 length, __u32 initval);

#endif				/* _OCFSHASH_H_ */
