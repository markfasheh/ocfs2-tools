/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * libtools-internal.h
 *
 * Internal header for libtools-internal.
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _LIBTOOLS_INTERNAL_H
#define _LIBTOOLS_INTERNAL_H

int tools_verbosity(void);
int tools_is_interactive(void);
void tools_progress_clear(void);
void tools_progress_restore(void);
int tools_progress_enabled(void);

#endif  /* _LIBTOOLS_INTERNAL_H */
