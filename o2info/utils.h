/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * utils.h
 *
 * Common utility function prototypes
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
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

#ifndef __UTILS_H__
#define __UTILS_H__

#include "o2info.h"

int o2info_get_compat_flag(uint32_t flag, char **compat);
int o2info_get_incompat_flag(uint32_t flag, char **incompat);
int o2info_get_rocompat_flag(uint32_t flag, char **rocompat);

int o2info_method(const char *path);

errcode_t o2info_open(struct o2info_method *om, int flags);
errcode_t o2info_close(struct o2info_method *om);

#endif		/* __UTILS_H__ */
