/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * byteorder.h
 *
 * Byteswapping!
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
 *
 * Authors: Joel Becker
 */

#ifndef _BYTEORDER_H
#define _BYTEORDER_H


#include <endian.h>
#include <byteswap.h>
#include <stdint.h>

/*
 * All OCFS2 on-disk values are in little endian, except for the
 * autoconfig areas and the journal areas.  The journal code has the
 * right bits for itself, and the autoconfig areas use htonl(), so
 * they are OK.
 */

#if __BYTE_ORDER == __LITTLE_ENDIAN
# ifndef cpu_to_le16
#  define cpu_to_le16(x) ((uint16_t)(x))
# endif
# ifndef le16_to_cpu
#  define le16_to_cpu(x) ((uint16_t)(x))
# endif
# ifndef cpu_to_le32
#  define cpu_to_le32(x) ((uint32_t)(x))
# endif
# ifndef le32_to_cpu
#  define le32_to_cpu(x) ((uint32_t)(x))
# endif
# ifndef cpu_to_le64
#  define cpu_to_le64(x) ((uint64_t)(x))
# endif
# ifndef le64_to_cpu
#  define le64_to_cpu(x) ((uint64_t)(x))
# endif
# ifndef cpu_to_be16
#  define cpu_to_be16(x) ((uint16_t)bswap_16(x))
# endif
# ifndef be16_to_cpu
#  define be16_to_cpu(x) ((uint16_t)bswap_16(x))
# endif
# ifndef cpu_to_be32
#  define cpu_to_be32(x) ((uint32_t)bswap_32(x))
# endif
# ifndef be32_to_cpu
#  define be32_to_cpu(x) ((uint32_t)bswap_32(x))
# endif
# ifndef cpu_to_be64
#  define cpu_to_be64(x) ((uint64_t)bswap_64(x))
# endif
# ifndef be64_to_cpu
#  define be64_to_cpu(x) ((uint64_t)bswap_64(x))
# endif
#elif __BYTE_ORDER == __BIG_ENDIAN
# ifndef cpu_to_le16
#  define cpu_to_le16(x) ((uint16_t)bswap_16(x))
# endif
# ifndef le16_to_cpu
#  define le16_to_cpu(x) ((uint16_t)bswap_16(x))
# endif
# ifndef cpu_to_le32
#  define cpu_to_le32(x) ((uint32_t)bswap_32(x))
# endif
# ifndef le32_to_cpu
#  define le32_to_cpu(x) ((uint32_t)bswap_32(x))
# endif
# ifndef cpu_to_le64
#  define cpu_to_le64(x) ((uint64_t)bswap_64(x))
# endif
# ifndef le64_to_cpu
#  define le64_to_cpu(x) ((uint64_t)bswap_64(x))
# endif
# ifndef cpu_to_be16
#  define cpu_to_be16(x) ((uint16_t)(x))
# endif
# ifndef be16_to_cpu
#  define be16_to_cpu(x) ((uint16_t)(x))
# endif
# ifndef cpu_to_be32
#  define cpu_to_be32(x) ((uint32_t)(x))
# endif
# ifndef be32_to_cpu
#  define be32_to_cpu(x) ((uint32_t)(x))
# endif
# ifndef cpu_to_be64
#  define cpu_to_be64(x) ((uint64_t)(x))
# endif
# ifndef be64_to_cpu
#  define be64_to_cpu(x) ((uint64_t)(x))
# endif
#else
# error Invalid byte order __BYTE_ORDER
#endif  /* __BYTE_ORDER */

#endif  /* _BYTEORDER_H */
