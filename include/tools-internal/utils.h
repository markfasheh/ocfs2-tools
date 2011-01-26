/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * utils.h
 *
 * Utility functions
 *
 * Copyright (C) 2010 Oracle.  All rights reserved.
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

#ifndef _INTERNAL_UTILS_H
#define _INTERNAL_UTILS_H

/*
 * Removes trailing whitespace from a string. It does not allocate or reallocate
 * any memory. It modifies the string in place.
 */
char *tools_strchomp(char *str);

/*
 * Removes leading whitespace from a string, by moving the rest of the
 * characters forward. It does not allocate or reallocate any memory.
 * It modifies the string in place.
 */
char *tools_strchug(char *str);

/*
 * Removes both the leading and trailing whitespaces
 */
#define tools_strstrip(str)	tools_strchug(tools_strchomp(str))

#endif  /* _INTERNAL_UTILS_H */
