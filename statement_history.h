/*-------------------------------------------------------------------------
 *
 * statement_history.h
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
 *	  contrib/statement_history/statement_history.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef STMT_HIST_H
#define STMT_HIST_H

#include "datatype/timestamp.h"
#include "nodes/nodes.h"
#include "portability/instr_time.h"

extern void _PG_init(void);         /* module load callback */


typedef struct SHTimeCalc
{
    instr_time  start;
    instr_time  end;
} SHTimeCalc;

typedef struct FormData_SHInternal
{
#define Anum_stmt_hist_internal_datid 1
#define Anum_stmt_hist_internal_userid 2
#define Anum_stmt_hist_internal_nspid 3
#define Anum_stmt_hist_internal_application_name 4
#define Anum_stmt_hist_internal_client_addr 5
#define Anum_stmt_hist_internal_client_port 6
#define Anum_stmt_hist_internal_query_string 7
#define Anum_stmt_hist_internal_queryid 8
#define Anum_stmt_hist_internal_start_time 9
#define Anum_stmt_hist_internal_finish_time 10
#define Anum_stmt_hist_internal_processid 11
#define Anum_stmt_hist_internal_n_soft_parse 12
#define Anum_stmt_hist_internal_n_hard_parse 13
#define Anum_stmt_hist_internal_query_plan 14
#define Anum_stmt_hist_internal_n_returned_rows 15
#define Anum_stmt_hist_internal_n_tuples_fetched 16
#define Anum_stmt_hist_internal_n_tuples_returned 17
#define Anum_stmt_hist_internal_n_tuples_inserted 18
#define Anum_stmt_hist_internal_n_tuples_updated 19
#define Anum_stmt_hist_internal_n_tuples_deleted 20
#define Anum_stmt_hist_internal_n_blocks_fetched 21
#define Anum_stmt_hist_internal_n_blocks_hit 22
#define Anum_stmt_hist_internal_db_time 23
#define Anum_stmt_hist_internal_cpu_time 24
#define Anum_stmt_hist_internal_io_time 25
#define Anum_stmt_hist_internal_parse_time 26
#define Anum_stmt_hist_internal_rewrite_time 27
#define Anum_stmt_hist_internal_plan_time 28
#define Anum_stmt_hist_internal_exec_time 29
#define Anum_stmt_hist_internal_lock_count 30
#define Anum_stmt_hist_internal_lock_time 31
#define Anum_stmt_hist_internal_lock_wait_count 32
#define Anum_stmt_hist_internal_lock_wait_time 33
#define Anum_stmt_hist_internal_lock_max_count 34
#define Anum_stmt_hist_internal_lwlock_count 35
#define Anum_stmt_hist_internal_lwlock_wait_count 36
#define Anum_stmt_hist_internal_lwlock_time 37
#define Anum_stmt_hist_internal_lwlock_wait_time 38
#define Anum_stmt_hist_internal_wait_event 39
#define Anum_stmt_hist_internal_is_slow_sql 40

#define Natts_stmt_hist_internal 40

    /*
     * The data fields in statement_history_internal table.
     */

    Oid                 datid;
    Oid                 userid;
    Oid                 nspid;

    /* client info attributes */
    char                *application_name;
    char                *client_addr;
    int16               client_port;

    /* query info attributes */
    char                *query_string;
    uint64              queryid;
    TimestampTz         start_time;
    TimestampTz         finish_time;
    int32               processid;
    int64               n_soft_parse;
    int64               n_hard_parse;
    char                *query_plan;

    /* query result info attributes */
    int64               n_returned_rows;
    int64               n_tuples_fetched;
    int64               n_tuples_returned;
    int64               n_tuples_inserted;
    int64               n_tuples_updated;
    int64               n_tuples_deleted;
    int64               n_blocks_fetched;
    int64               n_blocks_hit;

    /* execution info attributes */
    int64               db_time;
    int64               cpu_time;
    int64               io_time;
    int64               parse_time;
    int64               rewrite_time;
    int64               plan_time;
    int64               exec_time;

    /* lock info attribtes */
    int64               lock_count;
    int64               lock_time;
    int64               lock_wait_count;
    int64               lock_wait_time;
    int64               lock_max_count;
    int64               lwlock_count;
    int64               lwlock_wait_count;
    int64               lwlock_time;
    int64               lwlock_wait_time;
    char                *wait_event;

    /* other info */
    bool                is_slow_sql;
} FormData_SHInternal;

typedef struct SHDataInternalData
{
    int                 level;

    struct SHTimeCalc           parse_step;
    struct SHTimeCalc           rewrite_step;
    struct SHTimeCalc           plan_step;
    struct SHTimeCalc           exec_step;

    FormData_SHInternal         fd_shinternal;

    bool                        to_store;
} SHDataInternalData;

#endif							/* STMT_HIST_H */