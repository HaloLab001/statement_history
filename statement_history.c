/*-------------------------------------------------------------------------
 *
 * statement_history.c
 *		Like pg_stat_statements but more.
 *
 * 
 * 版权所有 (c) 2019-2024, 易景科技保留所有权利。
 * Copyright (c) 2019-2024, Halo Tech Co.,Ltd. All rights reserved.
 * 
 * 易景科技是Halo Database、Halo Database Management System、羲和数据
 * 库、羲和数据库管理系统（后面简称 Halo ）软件的发明人同时也为知识产权权
 * 利人。Halo 软件的知识产权，以及与本软件相关的所有信息内容（包括但不限
 * 于文字、图片、音频、视频、图表、界面设计、版面框架、有关数据或电子文档等）
 * 均受中华人民共和国法律法规和相应的国际条约保护，易景科技享有上述知识产
 * 权，但相关权利人依照法律规定应享有的权利除外。未免疑义，本条所指的“知识
 * 产权”是指任何及所有基于 Halo 软件产生的：（a）版权、商标、商号、域名、与
 * 商标和商号相关的商誉、设计和专利；与创新、技术诀窍、商业秘密、保密技术、非
 * 技术信息相关的权利；（b）人身权、掩模作品权、署名权和发表权；以及（c）在
 * 本协议生效之前已存在或此后出现在世界任何地方的其他工业产权、专有权、与“知
 * 识产权”相关的权利，以及上述权利的所有续期和延长，无论此类权利是否已在相
 * 关法域内的相关机构注册。
 *
 * This software and related documentation are provided under a license
 * agreement containing restrictions on use and disclosure and are 
 * protected by intellectual property laws. Except as expressly permitted
 * in your license agreement or allowed by law, you may not use, copy, 
 * reproduce, translate, broadcast, modify, license, transmit, distribute,
 * exhibit, perform, publish, or display any part, in any form, or by any
 * means. Reverse engineering, disassembly, or decompilation of this 
 * software, unless required by law for interoperability, is prohibited.
 * 
 * This software is developed for general use in a variety of
 * information management applications. It is not developed or intended
 * for use in any inherently dangerous applications, including applications
 * that may create a risk of personal injury. If you use this software or
 * in dangerous applications, then you shall be responsible to take all
 * appropriate fail-safe, backup, redundancy, and other measures to ensure
 * its safe use. Halo Corporation and its affiliates disclaim any 
 * liability for any damages caused by use of this software in dangerous
 * applications.
 * 
 *
 * IDENTIFICATION
 *	  contrib/statement_history/statement_history.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "miscadmin.h"
#include "statement_history.h"

#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/relscan.h"
#include "access/table.h"
#include "access/tableam.h"
#include "catalog/indexing.h"
#include "catalog/pg_class.h"
#include "catalog/pg_namespace.h"
#include "executor/executor.h"
#include "nodes/plannodes.h"
#include "optimizer/planner.h"
#include "parser/analyze.h"
#include "parser/parser.h"
#include "portability/instr_time.h"
#include "storage/lockdefs.h"
#include "utils/backend_status.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/rel.h"
#include "storage/lock.h"
#include "storage/lwlock.h"

/* GUC parameters*/
static bool sh_track_utility = true;	/* whether to track utility commands */

static SHDataInternalData   stmt_data;
static Oid  stmt_hist_internal_rel = InvalidOid;
static lock_statis lock_statis_data;

static pre_parse_hook_type  prev_pre_parse_hook = NULL;
static post_parse_hook_type prev_post_parse_hook = NULL;
static pre_parse_analyze_hook_type prev_pre_parse_analyze_hook = NULL;
static post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;
static planner_hook_type    prev_planner_hook= NULL;
static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static ExecutorRun_hook_type prev_ExecutorRun = NULL;
static ExecutorFinish_hook_type prev_ExecutorFinish = NULL;
static ExecutorEnd_hook_type prev_ExecutorEnd = NULL;

// lock_statisc_func_type lock_statis_hook = NULL;
// lock_statisc_end_type lock_statis_end_hook = NULL;
// lock_statisc_wait_type lock_statis_wait_hook = NULL;
// lock_statisc_wait_end_type lock_statis_wait_end_hook = NULL;

// static LWLock_statisc_hook_type lwlock_statisc_hook = NULL;
// static LWLock_statisc_end_type lwlock_statisc_end_hook = NULL;
// static LWLock_statisc_wait_type lwlock_statisc_wait_hook = NULL;

#define SH_INTERNAL_TABLE   "statement_history_internal"
#define SH_INTERNAL_TABNS  PG_CATALOG_NAMESPACE
#define SH_VALID_UTILSTMT(n)		(IsA(n, ExecuteStmt) || IsA(n, PrepareStmt))
/* hooks */
static void sh_pre_parse(const char *str, RawParseMode mode);
static void sh_post_parse(List	*parsetree);
static void sh_pre_parse_analyze(ParseState *pstate,
								 RawStmt *parseTree);
static void sh_post_parse_analyze(ParseState *pstate,
								  Query *query,
								  JumbleState *jstate);
static PlannedStmt *sh_planner(Query *parse,
                               const char *query_string,
			                   int cursorOptions,
			                   ParamListInfo boundParams);
static void sh_ExecutorStart(QueryDesc *queryDesc, int eflags);
static void sh_ExecutorRun(QueryDesc *queryDesc,
						   ScanDirection direction,
						   uint64 count,
						   bool execute_once);
static void sh_ExecutorFinish(QueryDesc *queryDesc);
static void sh_ExecutorEnd(QueryDesc *queryDesc);
static void sh_writedata(void);

static void statmh_lock_statisc_begin();
static void lock_statisc_begin();
static void lock_statisc_end();
static void lock_statisc_wait();
static void lock_statisc_wait_end();
static void lwlock_statisc_begin();
static void lwlock_statisc_end();
static void lwlock_statisc_wait();

PG_MODULE_MAGIC;

void
_PG_init(void)
{
    /* 
     * As we store the statement trace data in process local memory,
     * we needn't create shared memory area. So here is free to
     * check process_shared_preload_libraries_in_progress.
     */


    /*
	 * Inform the postmaster that we want to enable query_id calculation if
	 * compute_query_id is set to auto.
	 */
    EnableQueryId();

    /*
     * Define extension's GUC parameters
     */
    DefineCustomBoolVariable("statement_history.track_utility",
							 "Selects whether utility commands are tracked by statement_history.",
							 NULL,
							 &sh_track_utility,
							 false,
							 PGC_SUSET,
							 0,
							 NULL,
							 NULL,
							 NULL);


    /*
     * Setup all necessary hooks
     */
    prev_pre_parse_hook = pre_parse_hook;
    pre_parse_hook = sh_pre_parse;
    prev_post_parse_hook = post_parse_hook;
    post_parse_hook = sh_post_parse;
    prev_pre_parse_analyze_hook = pre_parse_analyze_hook;
    pre_parse_analyze_hook = sh_pre_parse_analyze;
    prev_post_parse_analyze_hook = post_parse_analyze_hook;
    post_parse_analyze_hook = sh_post_parse_analyze;
    prev_planner_hook = planner_hook;
    planner_hook = sh_planner;
    prev_ExecutorStart = ExecutorStart_hook;
	ExecutorStart_hook = sh_ExecutorStart;
	prev_ExecutorRun = ExecutorRun_hook;
	ExecutorRun_hook = sh_ExecutorRun;
	prev_ExecutorFinish = ExecutorFinish_hook;
	ExecutorFinish_hook = sh_ExecutorFinish;
	prev_ExecutorEnd = ExecutorEnd_hook;
	ExecutorEnd_hook = sh_ExecutorEnd;

    lock_statis_hook = lock_statisc_begin;
    lock_statis_end_hook = lock_statisc_end;
    lock_statis_wait_hook = lock_statisc_wait;
    lock_statis_wait_end_hook = lock_statisc_wait_end;

    lwlock_statisc_hook = lwlock_statisc_begin;
    lwlock_statisc_end_hook = lwlock_statisc_end;
    lwlock_statisc_wait_hook = lwlock_statisc_wait;
}

static void 
sh_pre_parse(const char *str, RawParseMode mode)
{
    int len;

    INSTR_TIME_SET_CURRENT(stmt_data.parse_step.start);

    if (prev_pre_parse_hook)
        prev_pre_parse_hook(str, mode);

    len = strlen(str);
    stmt_data.fd_shinternal.query_string = (char *) palloc(len + 1);
    memcpy(stmt_data.fd_shinternal.query_string, str, len);
    stmt_data.fd_shinternal.query_string[len] = '\0';

}

static void
sh_post_parse(List	*parsetree)
{
    if (prev_post_parse_hook)
        prev_post_parse_hook(parsetree);

    INSTR_TIME_SET_CURRENT(stmt_data.parse_step.end);

    INSTR_TIME_SUBTRACT(stmt_data.parse_step.end, stmt_data.parse_step.start);
    stmt_data.fd_shinternal.parse_time = INSTR_TIME_GET_MICROSEC(stmt_data.parse_step.end);
}

static void
sh_pre_parse_analyze(ParseState *pstate,
					 RawStmt *parseTree)
{
    INSTR_TIME_SET_CURRENT(stmt_data.rewrite_step.start);

    if (prev_pre_parse_analyze_hook)
        prev_pre_parse_analyze_hook(pstate, parseTree);
}

static void
sh_post_parse_analyze(ParseState *pstate,
					  Query *query,
					  JumbleState *jstate)
{
    if (prev_post_parse_analyze_hook)
        prev_post_parse_analyze_hook(pstate, query, jstate);

    if ((query->commandType == CMD_UTILITY && !sh_track_utility) ||
            query->commandType == CMD_UNKNOWN || 
            (sh_track_utility && query->utilityStmt && !SH_VALID_UTILSTMT(query->utilityStmt)))
        stmt_data.to_store = false;
    else if (sh_track_utility && query->utilityStmt && SH_VALID_UTILSTMT(query->utilityStmt))
    {
        query->queryId = UINT64CONST(0);
        stmt_data.to_store = true;
    }
    else
        stmt_data.to_store = true;
        
        
    
    stmt_data.fd_shinternal.queryid = query->queryId;

    INSTR_TIME_SET_CURRENT(stmt_data.rewrite_step.end);

    INSTR_TIME_SUBTRACT(stmt_data.rewrite_step.end, stmt_data.rewrite_step.start);
    stmt_data.fd_shinternal.rewrite_time = INSTR_TIME_GET_MICROSEC(stmt_data.rewrite_step.end);

    
}

static PlannedStmt *
sh_planner(Query *parse,
           const char *query_string,
		   int cursorOptions,
		   ParamListInfo boundParams)
{
    PlannedStmt *result;

    INSTR_TIME_SET_CURRENT(stmt_data.plan_step.start);

    if (prev_planner_hook)
        result = prev_planner_hook(parse, query_string, cursorOptions, boundParams);
    else
        result = standard_planner(parse, query_string, cursorOptions, boundParams);

    return result;
}

static void
sh_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
    INSTR_TIME_SET_CURRENT(stmt_data.plan_step.end);
    INSTR_TIME_SUBTRACT(stmt_data.plan_step.end, stmt_data.plan_step.start);
    stmt_data.fd_shinternal.plan_time = INSTR_TIME_GET_MICROSEC(stmt_data.plan_step.end);

    INSTR_TIME_SET_CURRENT(stmt_data.exec_step.start);
    if (prev_ExecutorStart)
		prev_ExecutorStart(queryDesc, eflags);
	else
		standard_ExecutorStart(queryDesc, eflags);
}

static void
sh_ExecutorRun(QueryDesc *queryDesc,
			   ScanDirection direction,
			   uint64 count,
			   bool execute_once)
{
    if (prev_ExecutorRun)
		prev_ExecutorRun(queryDesc, direction, count, execute_once);
	else
		standard_ExecutorRun(queryDesc, direction, count, execute_once);
}

static void
sh_ExecutorFinish(QueryDesc *queryDesc)
{
    if (prev_ExecutorFinish)
		prev_ExecutorFinish(queryDesc);
	else
		standard_ExecutorFinish(queryDesc);
}

static void
sh_ExecutorEnd(QueryDesc *queryDesc)
{
    if (prev_ExecutorEnd)
		prev_ExecutorEnd(queryDesc);
	else
		standard_ExecutorEnd(queryDesc);

    INSTR_TIME_SET_CURRENT(stmt_data.exec_step.end);
    INSTR_TIME_SUBTRACT(stmt_data.exec_step.end, stmt_data.exec_step.start);
    stmt_data.fd_shinternal.exec_time = INSTR_TIME_GET_MICROSEC(stmt_data.exec_step.end);

    sh_writedata();
}

static void
sh_writedata(void)
{
    Relation	rel_stmthist;
    HeapTuple   shtup;
    LocalPgBackendStatus *local_beentry;
	PgBackendStatus *beentry;
    Datum       values[Natts_stmt_hist_internal];
    bool        nulls[Natts_stmt_hist_internal];

    if (!stmt_data.to_store)
        return;


    if (stmt_hist_internal_rel == InvalidOid)
    {
        Relation    rel_class;
        TableScanDesc rc_scan;
	    HeapTuple	rc_tup;

        rel_class = table_open(RelationRelationId, AccessShareLock);
	    rc_scan = table_beginscan_catalog(rel_class, 0, NULL);

	    while ((rc_tup = heap_getnext(rc_scan, ForwardScanDirection)) != NULL)
	    {
		    Form_pg_class rcForm = (Form_pg_class) GETSTRUCT(rc_tup);

            if (rcForm->relnamespace == SH_INTERNAL_TABNS &&
                    strcasecmp(rcForm->relname.data, SH_INTERNAL_TABLE) == 0)
            {
                stmt_hist_internal_rel = rcForm->oid;

                Assert(stmt_hist_internal_rel != InvalidOid);

                break;
            }
	    }

	    table_endscan(rc_scan);
	    table_close(rel_class, AccessShareLock);
    }

    local_beentry = pgstat_get_local_beentry_by_proc_number(MyProcNumber);
    beentry  = &local_beentry->backendStatus;

    values[Anum_stmt_hist_internal_datid - 1] = ObjectIdGetDatum(MyDatabaseId);
    nulls[Anum_stmt_hist_internal_datid - 1] = false;

    values[Anum_stmt_hist_internal_userid - 1] = Int32GetDatum(beentry->st_userid);
    nulls[Anum_stmt_hist_internal_userid - 1] = false;

    values[Anum_stmt_hist_internal_nspid - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_nspid - 1] = true;

    if (strcmp(beentry->st_appname, "") == 0)
    {
        values[Anum_stmt_hist_internal_application_name - 1] = (Datum) 0;
        nulls[Anum_stmt_hist_internal_application_name - 1] = true;
    }
    else
    {
        values[Anum_stmt_hist_internal_application_name - 1] = PointerGetDatum(cstring_to_text(beentry->st_appname));
        nulls[Anum_stmt_hist_internal_application_name - 1] = false;
    }
    
    if (strcmp(beentry->st_clienthostname, "") == 0)
    {
        values[Anum_stmt_hist_internal_client_addr - 1] = (Datum) 0;
        nulls[Anum_stmt_hist_internal_client_addr - 1] = true;
    }
    else
    {
        values[Anum_stmt_hist_internal_client_addr - 1] = PointerGetDatum(cstring_to_text(beentry->st_clienthostname));
        nulls[Anum_stmt_hist_internal_client_addr - 1] = false;
    }
    

    values[Anum_stmt_hist_internal_client_port - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_client_port - 1] = true;

    if (strcmp(stmt_data.fd_shinternal.query_string, "") == 0)
    {
        values[Anum_stmt_hist_internal_query_string - 1] = (Datum) 0;
        nulls[Anum_stmt_hist_internal_query_string - 1] = true;
    }
    else
    {
        values[Anum_stmt_hist_internal_query_string - 1] = PointerGetDatum(cstring_to_text(stmt_data.fd_shinternal.query_string));
        nulls[Anum_stmt_hist_internal_query_string - 1] = false;
    }

    values[Anum_stmt_hist_internal_queryid - 1] = Int64GetDatum(stmt_data.fd_shinternal.queryid);
    nulls[Anum_stmt_hist_internal_queryid - 1] = false;

    values[Anum_stmt_hist_internal_start_time - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_start_time - 1] = true;

    values[Anum_stmt_hist_internal_finish_time - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_finish_time - 1] = true;

    values[Anum_stmt_hist_internal_processid - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_processid - 1] = true;

    values[Anum_stmt_hist_internal_n_soft_parse - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_n_soft_parse - 1] = true;

    values[Anum_stmt_hist_internal_n_hard_parse - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_n_hard_parse - 1] = true;

    values[Anum_stmt_hist_internal_query_plan - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_query_plan - 1] = true;

    values[Anum_stmt_hist_internal_n_returned_rows - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_n_returned_rows - 1] = true;

    values[Anum_stmt_hist_internal_n_tuples_fetched - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_n_tuples_fetched - 1] = true;

    values[Anum_stmt_hist_internal_n_tuples_returned - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_n_tuples_returned - 1] = true;

    values[Anum_stmt_hist_internal_n_tuples_inserted - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_n_tuples_inserted - 1] = true;

    values[Anum_stmt_hist_internal_n_tuples_updated - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_n_tuples_updated - 1] = true;

    values[Anum_stmt_hist_internal_n_tuples_deleted - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_n_tuples_deleted - 1] = true;

    values[Anum_stmt_hist_internal_n_blocks_fetched - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_n_blocks_fetched - 1] = true;

    values[Anum_stmt_hist_internal_n_blocks_hit - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_n_blocks_hit - 1] = true;

    values[Anum_stmt_hist_internal_db_time - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_db_time - 1] = true;

    values[Anum_stmt_hist_internal_cpu_time - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_cpu_time - 1] = true;

    values[Anum_stmt_hist_internal_io_time - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_io_time - 1] = true;

    values[Anum_stmt_hist_internal_parse_time - 1] = Int64GetDatum(stmt_data.fd_shinternal.parse_time);
    nulls[Anum_stmt_hist_internal_parse_time - 1] = false;

    values[Anum_stmt_hist_internal_rewrite_time - 1] = Int64GetDatum(stmt_data.fd_shinternal.rewrite_time);
    nulls[Anum_stmt_hist_internal_rewrite_time - 1] = false;

    values[Anum_stmt_hist_internal_plan_time - 1] = Int64GetDatum(stmt_data.fd_shinternal.plan_time);
    nulls[Anum_stmt_hist_internal_plan_time - 1] = false;

    values[Anum_stmt_hist_internal_exec_time - 1] = Int64GetDatum(stmt_data.fd_shinternal.exec_time);
    nulls[Anum_stmt_hist_internal_exec_time - 1] = false;

    values[Anum_stmt_hist_internal_lock_count - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_lock_count - 1] = true;

    values[Anum_stmt_hist_internal_lock_time - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_lock_time - 1] = true;

    values[Anum_stmt_hist_internal_lock_wait_count - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_lock_wait_count - 1] = true;

    values[Anum_stmt_hist_internal_lock_wait_time - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_lock_wait_time - 1] = true;

    values[Anum_stmt_hist_internal_lock_max_count - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_lock_max_count - 1] = true;

    values[Anum_stmt_hist_internal_lwlock_count - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_lwlock_count] = true;

    values[Anum_stmt_hist_internal_lwlock_wait_count - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_lwlock_wait_count - 1] = true;

    values[Anum_stmt_hist_internal_lwlock_time - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_lwlock_time - 1] = true;

    values[Anum_stmt_hist_internal_lwlock_wait_time - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_lwlock_wait_time - 1] = true;

    values[Anum_stmt_hist_internal_wait_event - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_wait_event - 1] = true;

    values[Anum_stmt_hist_internal_is_slow_sql - 1] = (Datum) 0;
    nulls[Anum_stmt_hist_internal_is_slow_sql - 1] = true;

    rel_stmthist = table_open(stmt_hist_internal_rel, NoLock);
    shtup = heap_form_tuple(RelationGetDescr(rel_stmthist), values, nulls);
    CatalogTupleInsert(rel_stmthist, shtup);
    table_close(rel_stmthist, NoLock);
}

/*
    run at SQL startted. When sql finished, no function have to run.
*/
static void 
statmh_lock_statisc_begin()
{
    /* for lock */
    stmt_data.fd_shinternal.lock_count = 0;
    stmt_data.fd_shinternal.lock_wait_count = 0;
    stmt_data.fd_shinternal.lock_time = 0;
    stmt_data.fd_shinternal.lock_wait_time = 0;

    INSTR_TIME_SET_ZERO(lock_statis_data.lock_time);
    INSTR_TIME_SET_ZERO(lock_statis_data.lock_wttime);
    lock_statis_data.recursion = 0;

    /* for lwlock */
    stmt_data.fd_shinternal.lwlock_count = 0;
    stmt_data.fd_shinternal.lwlock_wait_count = 0;
    stmt_data.fd_shinternal.lwlock_time = 0;
    stmt_data.fd_shinternal.lwlock_wait_time = 0;

    INSTR_TIME_SET_ZERO(lock_statis_data.lwlock_time);
    INSTR_TIME_SET_ZERO(lock_statis_data.lwlock_wttime);
    lock_statis_data.lwlock_stat = LOCK_STATISC_NOWAIT;

    return ;
}

/* lock */
static void
lock_statisc_begin()
{
    stmt_data.fd_shinternal.lock_count++;          // lock gets ++
    lock_statis_data.recursion ++;

    if (lock_statis_data.recursion <= 1)
    {
        INSTR_TIME_SET_CURRENT(lock_statis_data.lock_time);
    }
}

static void
lock_statisc_end()
{
    instr_time      duration;
    TimestampTz     tm;

    if (lock_statis_data.recursion <= 1)
    {
        INSTR_TIME_SET_CURRENT(duration);
        INSTR_TIME_SUBTRACT(duration, lock_statis_data.lock_time);
        tm = INSTR_TIME_GET_MICROSEC(duration);
        stmt_data.fd_shinternal.lock_time += tm;
    }

    lock_statis_data.recursion --;

    return ;
}

static void
lock_statisc_wait()
{
    stmt_data.fd_shinternal.lock_wait_count++;          // lock gets ++
    INSTR_TIME_SET_CURRENT(lock_statis_data.lock_wttime);
}

static void
lock_statisc_wait_end()
{
    instr_time      duration;
    TimestampTz     tm;

    INSTR_TIME_SET_CURRENT(duration);
    INSTR_TIME_SUBTRACT(duration, lock_statis_data.lock_wttime);
    tm = INSTR_TIME_GET_MICROSEC(duration);
    stmt_data.fd_shinternal.lock_wait_time += tm;

    return ;
}

/* LWLock */
static void
lwlock_statisc_begin()
{
    instr_time      duration;
    TimestampTz     tm;

    stmt_data.fd_shinternal.lwlock_count++;          // LWLock gets ++
    INSTR_TIME_SET_CURRENT(lock_statis_data.lwlock_time);

    /* Happy Path */
    if (lock_statis_data.lwlock_stat != LOCK_STATISC_WAITTING)
    {
        return ;
    }

    /* 计算LWLock等待时间 */
    INSTR_TIME_SET_CURRENT(duration);
    INSTR_TIME_SUBTRACT(duration, lock_statis_data.lwlock_wttime);
    tm = INSTR_TIME_GET_MICROSEC(duration);
    stmt_data.fd_shinternal.lwlock_wait_time += tm;
    lock_statis_data.lwlock_stat = LOCK_STATISC_NOWAIT;
    INSTR_TIME_SET_ZERO(lock_statis_data.lwlock_wttime);    // 等待状态设为LOCK_STATISC_NOWAIT时，lwlock_wttime必须清0。
}

static void
lwlock_statisc_end()
{
    instr_time      duration;
    TimestampTz     tm;

    INSTR_TIME_SET_CURRENT(duration);
    INSTR_TIME_SUBTRACT(duration, lock_statis_data.lwlock_time);
    tm = INSTR_TIME_GET_MICROSEC(duration);
    stmt_data.fd_shinternal.lwlock_time += tm;

    /* 恢复原始值 */
    INSTR_TIME_SET_ZERO(lock_statis_data.lwlock_time);
    INSTR_TIME_SET_ZERO(lock_statis_data.lwlock_wttime);
    lock_statis_data.lwlock_stat = LOCK_STATISC_NOWAIT;
}

static void
lwlock_statisc_wait()
{
    /* 一次加锁尝试结束，开始等待。等待被唤醒后，会再次进入加锁函数 */
    lwlock_statisc_end();

    stmt_data.fd_shinternal.lwlock_wait_count++;          // LWLock wait ++

    lock_statis_data.lwlock_stat = LOCK_STATISC_WAITTING;
    INSTR_TIME_SET_CURRENT(lock_statis_data.lwlock_wttime);
}
