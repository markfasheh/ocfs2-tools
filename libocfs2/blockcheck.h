/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * blockcheck.h
 *
 * Checksum and ECC codes for the OCFS2 userspace library.
 *
 * Copyright (C) 2004, 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _BLOCKCHECK_H
#define _BLOCKCHECK_H

extern uint32_t ocfs2_hamming_encode(uint32_t parity, void *data,
				     unsigned int d, unsigned int nr);
extern uint32_t ocfs2_hamming_encode_block(void *data, unsigned int d);
extern void ocfs2_hamming_fix(void *data, unsigned int d, unsigned int nr,
			      unsigned int fix);
extern void ocfs2_hamming_fix_block(void *data, unsigned int d,
				    unsigned int fix);
extern uint32_t crc32_le(uint32_t crc, unsigned char const *p, size_t len);
#endif
