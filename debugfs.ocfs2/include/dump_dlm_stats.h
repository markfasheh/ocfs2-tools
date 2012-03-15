/*
 * dump_dlm_stats.h
 *
 * Function prototypes, macros, etc. for related 'C' files
 *
 * Copyright (C) 20011 Oracle.  All rights reserved.
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
 */

#ifndef _DUMP_DLM_STATS_H_
#define _DUMP_DLM_STATS_H_

enum dlm_mle_type {
	DLM_MLE_BLOCK = 0,
	DLM_MLE_MASTER = 1,
	DLM_MLE_MIGRATION = 2,
	DLM_MLE_NUM_TYPES = 3,
};

struct dlm_object_counters {
	long long oc_res_total;
	long long oc_mle_total[DLM_MLE_NUM_TYPES];
	unsigned long oc_res_alive;
	unsigned long oc_mle_alive[DLM_MLE_NUM_TYPES];
};

struct dlm_lookup_counters {
	long long lc_succ_total;
	long long lc_succ_nsecs;
	long long lc_fail_total;
	long long lc_fail_nsecs;
};

struct dlm_migrate_counters {
	long long mc_succ_total;
	long long mc_succ_nsecs;
	long long mc_fail_total;
};

struct dlm_mastery_counters {
	long long ma_local_total;	/* mastery started by this node */
	long long ma_local_nsecs;
	long long ma_joind_total;	/* mastery started by another node */
	long long ma_joind_nsecs;
};

enum {
	DLM_STATS_NL_TO_PR = 0,
	DLM_STATS_NL_TO_EX,
	DLM_STATS_PR_TO_EX,
	DLM_STATS_PR_TO_NL,
	DLM_STATS_EX_TO_PR,
	DLM_STATS_EX_TO_NL,
	DLM_STATS_NUM_CNVTS
};

struct dlm_convert_counters {
	long long cc_local_total[DLM_STATS_NUM_CNVTS];
	long long cc_local_nsecs[DLM_STATS_NUM_CNVTS];
	long long cc_remot_total[DLM_STATS_NUM_CNVTS];
	long long cc_remot_nsecs[DLM_STATS_NUM_CNVTS];
};

struct dlm_stats {
	struct dlm_object_counters oc;
	struct dlm_lookup_counters res;
	struct dlm_lookup_counters mle;
	struct dlm_migrate_counters mc;
	struct dlm_mastery_counters ma;
	struct dlm_convert_counters cc;
};
void dump_dlm_stats(FILE *out, char *uuid, int interval, int count);

#endif
