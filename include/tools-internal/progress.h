/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * progress.h
 *
 * Internal progress output functions.
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

#ifndef _INTERNAL_PROGRESS_H
#define _INTERNAL_PROGRESS_H

struct tools_progress;

/*
 * Enables progress display.  This is generally called when a program
 * is passed the '--progress' option.
 */
void tools_progress_enable(void);
/*
 * Disable progress display.  This is a true disable.  Users do not need
 * to disable by hand when using verbosef() and friends for output.  They
 * interact correctly with the progress display.
 */
void tools_progress_disable(void);

/*
 * Returns 1 if progress display is enabled, 0 if not.
 */
int tools_progress_enabled(void);

/*
 * Callers should use the progress API unconditionally.  If the progress
 * display is not enabled, the functions are no-ops.
 *
 * The progress bar can contain multiple progress displays.  A toplevel
 * action may run a sub-action that takes time.  Thus, the bar displays
 * both the progress state of the toplevel action and the sub-action.
 * 
 * The sub-action can start its own progress display without knowledge
 * of the toplevel.  The progress code understands how to handle it.
 */

/*
 * Start a new progress item.
 *
 * The long name should be no longer than 25 characters or so.  The
 * short name really wants to be no more than 8.  When displaying the
 * progress bar, the progress code sees if the long names of all
 * registered progress items can fit.  If not, it will use the short
 * names, starting at the outermost progress item.  If the short names
 * don't fit, it will truncate the short names.
 *
 * count is how many steps are required for completion of this progress
 * item.  If count is non-zero, the progress bar will display a
 * completion percentage.  If count is zero, the item is considered
 * unbounded, and the progress bar will display a simple spinner.
 *
 * A new progress item is returned.  If NULL is returned, its because
 * there is no memory available.
 */
struct tools_progress *tools_progress_start(const char *long_name,
					    const char *short_name,
					    uint64_t count);
/*
 * Increment the progress item.
 *
 * step is the number of steps to add to the completed count.  This will
 * update the progress bar.  As an optimization, the bar is changed at
 * most once every 1/8s.  In addition, it will not be updated if the
 * completion percentage has not changed.
 */
void tools_progress_step(struct tools_progress *prog, unsigned int step);

/*
 * Stop this progress item.  This will free the item and remove it from the
 * progress bar.
 */
void tools_progress_stop(struct tools_progress *prog);
#endif  /* _INTERNAL_PROGRESS_H */
