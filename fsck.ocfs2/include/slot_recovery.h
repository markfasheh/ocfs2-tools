/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * slot_recovery.c
 *
 * Slot recovery handler.
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __O2FSCK_SLOT_RECOVERY_H__
#define __O2FSCK_SLOT_RECOVERY_H__

#include "fsck.h"

errcode_t o2fsck_replay_truncate_logs(ocfs2_filesys *fs);

#endif /* __O2FSCK_SLOT_RECOVERY_H__ */

