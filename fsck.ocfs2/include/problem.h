/*
 * problem.h
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
 * Author: Zach Brown
 */

#ifndef __O2FSCK_PROBLEM_H__
#define __O2FSCK_PROBLEM_H__

/* prompt flags. */
#define PY (1 << 0) /* default to yes when asked and no answer forced */
#define PN  (1 << 1) /* default to no when asked and no answer forced */

#include "fsck.h"

/* returns non-zero for yes and zero for no.  The caller is expected to
 * provide a thorough description of the state and the action that will
 * be taken depending on the answer.  Without \n termination.
 *
 * The code argument is a digit that identifies the error.  it is used by
 * input to regression testing utils and referenced in fsck.ocfs2.checks(8).
 * each code is only supposed to have one call site.  we create this
 * funny symbol so that the 'check-prompt-callers' makefile target 
 * can go groveling through nm to find out if a code has more than
 * one call site.
 */
#define prompt(ost, flags, code, fmt...) ({			\
	static int fsck_prompt_callers_with_code_##code = code;	\
	int _ret = fsck_prompt_callers_with_code_##code;	\
	_ret = prompt_input(ost, flags, code, fmt);		\
	_ret;							\
})

int prompt_input(o2fsck_state *ost, unsigned flags, uint16_t code, 
		 const char *fmt, ...)
	 __attribute__ ((format (printf, 4, 5)));

#endif /* __O2FSCK_PROBLEM_H__ */

