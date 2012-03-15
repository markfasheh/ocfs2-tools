/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dump_dlm_stats.c
 *
 * Interface with the kernel and show dlm locking statistics
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
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
 */

#define _XOPEN_SOURCE 600  /* Triggers XOPEN2K in features.h */
#define _LARGEFILE64_SOURCE

#include "main.h"
#include "ocfs2/byteorder.h"
#include "ocfs2_internals.h"

static char *cmd = "dlm_stats";

#if 0


--------NL->PR--------- --------NL->PR--------- --------NL->PR--------- --------NL->PR--------- --------NL->PR---------
---local--- ---rocal--- ---local--- ---rocal--- ---local--- ---rocal--- ---local--- ---rocal--- ---local--- ---rocal---
999k 012.3u 999k 012.3u 999k 012.3u 999k 012.3u 999k 012.3u 999k 012.3u 999k 012.3u 999k 012.3u 999k 012.3u 999k 012.3u


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



#endif

static void show_convert_stats(FILE *out, struct dlm_stats *prev,
			       struct dlm_stats *curr, int interval,
			       unsigned long proto)
{
	struct dlm_convert_counters _cc = *cc = &_cc;

	fprintf(out, "%s %s %s %s %s %s\n",
		"--------NL->PR---------",
		"--------NL->EX---------",
		"--------PR->EX---------",
		"--------PR->NL---------",
		"--------EX->PR---------",
		"--------EX->NL---------");

}

static void show_mastery_stats(FILE *out, struct dlm_stats *prev,
			       struct dlm_stats *curr, int interval,
			       unsigned long proto)
{
	struct dlm_migrate_counters _mc, *mc = &_mc;
	struct dlm_mastery_counters _ma, *ma = &_ma;
	double ma_local_nsecs = 0, ma_joind_nsecs = 0, mc_succ_nsecs = 0;

	memcpy(mc, &curr->mc, sizeof(struct dlm_migrate_counters));
	memcpy(ma, &curr->ma, sizeof(struct dlm_mastery_counters));

	if (prev) {
		ma->ma_local_total -= prev->ma.ma_local_total;
		ma->ma_local_nsecs -= prev->ma.ma_local_nsecs;
		ma->ma_joind_total -= prev->ma.ma_joind_total;
		ma->ma_joind_nsecs -= prev->ma.ma_joind_nsecs;
		mc->mc_succ_total -= prev->mc.mc_succ_total;
		mc->mc_succ_nsecs -= prev->mc.mc_succ_nsecs;
		mc->mc_fail_total -= prev->mc.mc_fail_total;
	} else {
		fprintf(out, "%s  %s  %s\n", "------initiated------",
			"--------joined-------",
			"-----------migrated------------");
		fprintf(out, "%10s %10s  %10s %10s  %10s %10s %10s\n", "count",
			"usecs", "count", "usecs", "fail", "count", "usecs");
	}

	ma_local_nsecs = ma->ma_local_nsecs;
	ma_joind_nsecs = ma->ma_joind_nsecs;
	mc_succ_nsecs = mc->mc_succ_nsecs;

	if (ma->ma_local_total)
		ma_local_nsecs /= (ma->ma_local_total * 1000);
	if (ma->ma_joind_total)
		ma_joind_nsecs /= (ma->ma_joind_total * 1000);
	if (mc->mc_succ_total)
		mc_succ_nsecs /= (mc->mc_succ_total * 1000);

	fprintf(out, "%10lld %10.2f  %10lld %10.2f  %10lld %10lld %10.2f\n",
		ma->ma_local_total, ma_local_nsecs, ma->ma_joind_total,
		ma_joind_nsecs, mc->mc_fail_total, mc->mc_succ_total,
		mc_succ_nsecs);
}

static void show_lookup_stats(FILE *out, struct dlm_stats *prev,
			      struct dlm_stats *curr, int interval,
			      unsigned long proto)
{
	unsigned long i, mle_alive = 0;
	struct dlm_object_counters _oc, *oc = &_oc;
	struct dlm_lookup_counters _res, *res = &_res;
	struct dlm_lookup_counters _mle, *mle = &_mle;

	memcpy(oc, &curr->oc, sizeof(struct dlm_object_counters));
	memcpy(res, &curr->res, sizeof(struct dlm_lookup_counters));
	memcpy(mle, &curr->mle, sizeof(struct dlm_lookup_counters));
	for (i = DLM_MLE_BLOCK; i < DLM_MLE_NUM_TYPES; ++i)
		mle_alive += oc->oc_mle_alive[i];

#define oc_diff(a, b)	\
	do {	\
		(a)->lc_succ_total -= (b)->lc_succ_total;	\
		(a)->lc_succ_nsecs -= (b)->lc_succ_nsecs;	\
		(a)->lc_fail_total -= (b)->lc_fail_total;	\
		(a)->lc_fail_nsecs -= (b)->lc_fail_nsecs;	\
	} while (0)

	if (prev) {
		oc_diff(res, &(prev->res));
		oc_diff(mle, &(prev->mle));
	} else {
		fprintf(out, "%s  %s\n",
			"------------------lock resources-----------------",
			"-------------master list entries-------------");
		fprintf(out, "%10s %10s %8s %10s %8s  %6s %10s %8s %10s %8s\n",
			"count", "success", "nsecs", "fail", "nsecs",
			"count", "success", "nsecs", "fail", "nsecs");
	}

	if (res->lc_succ_total)
		res->lc_succ_nsecs /= res->lc_succ_total;
	if (res->lc_fail_total)
		res->lc_fail_nsecs /= res->lc_fail_total;
	if (mle->lc_succ_total)
		mle->lc_succ_nsecs /= mle->lc_succ_total;
	if (mle->lc_fail_total)
		mle->lc_fail_nsecs /= mle->lc_fail_total;


	fprintf(out, "%10lu %10lld %8lld %10lld %8lld  "
		"%6lu %10lld %8lld %10lld %8lld\n",
		oc->oc_res_alive, res->lc_succ_total, res->lc_succ_nsecs,
		res->lc_fail_total, res->lc_fail_nsecs, mle_alive,
		mle->lc_succ_total, mle->lc_succ_nsecs, mle->lc_fail_total,
		mle->lc_fail_nsecs);
}

static errcode_t read_convert_counters(struct dlm_convert_counters *cc,
				       char **str, char **remstr,
				       unsigned long proto)
{
	errcode_t ret = OCFS2_ET_INTERNAL_FAILURE;
	int start = DLM_STATS_NL_TO_PR, end;
	int i, out;

	end = (proto == 1) ? DLM_STATS_NUM_CNVTS - 1 : DLM_STATS_NUM_CNVTS;

	for (i = start; i < end; ++i) {
		out = sscanf(*str,
			     "%llu,%llu,%llu,%llu,%s",
			     &cc->cc_local_total[i],
			     &cc->cc_local_nsecs[i],
			     &cc->cc_remot_total[i],
			     &cc->cc_remot_nsecs[i],
			     *remstr);
		if (out != 5) {
			com_err(cmd, ret, "Error reading convert counters\n");
			goto bail;
		}

		dbfs_swap(*str, *remstr);
	}

	if (end == DLM_STATS_NUM_CNVTS - 1) {
		out = sscanf(*str,
			     "%llu,%llu,%llu,%llu",
			     &cc->cc_local_total[end],
			     &cc->cc_local_nsecs[end],
			     &cc->cc_remot_total[end],
			     &cc->cc_remot_nsecs[end]);
		if (out != 4) {
			com_err(cmd, ret, "Error reading convert counters\n");
			goto bail;
		}
		*remstr = '\0';
	}

	dbfs_swap(*str, *remstr);

	ret = 0;
bail:
	return ret;
}

static errcode_t read_mastery_counters(struct dlm_mastery_counters *ma,
				       char *str, char *remstr,
				       unsigned long proto)
{
	errcode_t ret = OCFS2_ET_INTERNAL_FAILURE;
	int out;

	out = sscanf(str,
		     "%llu,%llu,%llu,%llu,%s",
		     &ma->ma_local_total,
		     &ma->ma_local_nsecs,
		     &ma->ma_joind_total,
		     &ma->ma_joind_nsecs,
		     remstr);
	if (out != 5) {
		com_err(cmd, ret, "Error reading mastery counters\n");
		goto bail;
	}

	ret = 0;
bail:
	return ret;
}

static errcode_t read_migrate_counters(struct dlm_migrate_counters *mc,
				       char *str, char *remstr,
				       unsigned long proto)
{
	errcode_t ret = OCFS2_ET_INTERNAL_FAILURE;
	int out;

	out = sscanf(str,
		     "%llu,%llu,%llu,%s",
		     &mc->mc_succ_total,
		     &mc->mc_succ_nsecs,
		     &mc->mc_fail_total,
		     remstr);
	if (out != 4) {
		com_err(cmd, ret, "Error reading migration counters\n");
		goto bail;
	}

	ret = 0;
bail:
	return ret;
}

static errcode_t read_lookup_counters(struct dlm_lookup_counters *res,
				      struct dlm_lookup_counters *mle,
				      char *str, char *remstr,
				      unsigned long proto)
{
	errcode_t ret = OCFS2_ET_INTERNAL_FAILURE;
	int out;

	out = sscanf(str,
		     "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%s",
		     &res->lc_succ_total,
		     &res->lc_succ_nsecs,
		     &res->lc_fail_total,
		     &res->lc_fail_nsecs,
		     &mle->lc_succ_total,
		     &mle->lc_succ_nsecs,
		     &mle->lc_fail_total,
		     &mle->lc_fail_nsecs,
		     remstr);
	if (out != 9) {
		com_err(cmd, ret, "Error reading lookup counters\n");
		goto bail;
	}

	ret = 0;
bail:
	return ret;
}

static errcode_t read_object_counters(struct dlm_object_counters *oc,
				      char *str, char *remstr,
				      unsigned long proto)
{
	errcode_t ret = OCFS2_ET_INTERNAL_FAILURE;
	int out;

	out = sscanf(str,
		     "%lu,%lu,%lu,%lu,%s",
		     &oc->oc_res_alive,
		     &oc->oc_mle_alive[DLM_MLE_BLOCK],
		     &oc->oc_mle_alive[DLM_MLE_MASTER],
		     &oc->oc_mle_alive[DLM_MLE_MIGRATION],
		     remstr);
	if (out != 5) {
		com_err(cmd, ret, "Error reading object counters\n");
		goto bail;
	}

	ret = 0;
bail:
	return ret;
}

#define CURRENT_DLM_STATS_PROTO		1
#define MAX_DLM_STATS_STR_LEN		4096

static errcode_t read_dlm_stats(const char *debugfs_path, char *uuid,
				struct dlm_stats *stats, unsigned long *proto)
{
	FILE *file = NULL;
	char rec1[MAX_DLM_STATS_STR_LEN], rec2[MAX_DLM_STATS_STR_LEN];
	char *src, *dst;
	errcode_t ret;
	int out;
	struct dlm_object_counters *oc = &(stats->oc);
	struct dlm_lookup_counters *res = &(stats->res);
	struct dlm_lookup_counters *mle = &(stats->mle);
	struct dlm_migrate_counters *mc = &(stats->mc);
	struct dlm_mastery_counters *ma = &(stats->ma);
	struct dlm_convert_counters *cc = &(stats->cc);

	memset(stats, 0, sizeof(struct dlm_stats));

	ret = open_debugfs_file(debugfs_path, "o2dlm", uuid, "stats", &file);
	if (ret) {
		com_err(cmd, ret, "Could not open %s/o2dlm/%s/stats\n",
			debugfs_path, uuid);
		goto bail;
	}

	while (fgets(rec1, sizeof(rec1), file)) {
		ret = OCFS2_ET_INTERNAL_FAILURE;
		src = rec1;
		dst = rec2;

		/* read protocol version */
		out = sscanf(src, "%lu,%s\n", proto, dst);
		if (out != 2) {
			com_err(cmd, ret, "Error reading version\n");
			goto bail;
		}
		dbfs_swap(src, dst);

		if (*proto > CURRENT_DLM_STATS_PROTO) {
			com_err(cmd, ret, "o2dlm stats proto %lu found, but %u "
				"is the highest I understand.\n", *proto,
				CURRENT_DLM_STATS_PROTO);
			goto bail;
		}

		/* Protocol version 1 - begin */

		ret = read_object_counters(oc, src, dst, *proto);
		if (ret)
			goto bail;
		dbfs_swap(src, dst);

		ret = read_lookup_counters(res, mle, src, dst, *proto);
		if (ret)
			goto bail;
		dbfs_swap(src, dst);

		ret = read_migrate_counters(mc, src, dst, *proto);
		if (ret)
			goto bail;
		dbfs_swap(src, dst);

		ret = read_mastery_counters(ma, src, dst, *proto);
		if (ret)
			goto bail;
		dbfs_swap(src, dst);

		ret = read_convert_counters(cc, &src, &dst, *proto);
		if (ret)
			goto bail;
		dbfs_swap(src, dst);

		/* Protocol version 1 - end */
	}

	ret = 0;

bail:
	if (file)
		fclose(file);

	return ret;
}

void dump_dlm_stats(FILE *out, char *uuid, int interval, int count)
{
	errcode_t ret;
	char debugfs_path[PATH_MAX];
	struct dlm_stats buf1, buf2, *curr = &buf1, *prev = NULL;
	unsigned long proto;

	ret = get_debugfs_path(debugfs_path, sizeof(debugfs_path));
	if (ret) {
		fprintf(stderr, "Could not locate debugfs file system. "
			"Perhaps it is not mounted?\n");
		return;
	}

	count = (count == 0) ? -1 : count;

	do {
		ret = read_dlm_stats(debugfs_path, uuid, curr, &proto);
		if (ret)
			break;

		show_mastery_stats(out, prev, curr, interval, proto);

		if (!interval)
			break;

		if (count > 0 && !--count)
			break;

		dbfs_swap(prev, curr);

		/* first time */
		if (!curr)
			curr = &buf2;

		sleep(interval);

	} while(1);
}
