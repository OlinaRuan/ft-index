/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
// vim: ft=cpp:expandtab:ts=8:sw=4:softtabstop=4:
/*
COPYING CONDITIONS NOTICE:

  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation, and provided that the
  following conditions are met:

      * Redistributions of source code must retain this COPYING
        CONDITIONS NOTICE, the COPYRIGHT NOTICE (below), the
        DISCLAIMER (below), the UNIVERSITY PATENT NOTICE (below), the
        PATENT MARKING NOTICE (below), and the PATENT RIGHTS
        GRANT (below).

      * Redistributions in binary form must reproduce this COPYING
        CONDITIONS NOTICE, the COPYRIGHT NOTICE (below), the
        DISCLAIMER (below), the UNIVERSITY PATENT NOTICE (below), the
        PATENT MARKING NOTICE (below), and the PATENT RIGHTS
        GRANT (below) in the documentation and/or other materials
        provided with the distribution.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
  02110-1301, USA.

COPYRIGHT NOTICE:

  TokuFT, Tokutek Fractal Tree Indexing Library.
  Copyright (C) 2007-2013 Tokutek, Inc.

DISCLAIMER:

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

UNIVERSITY PATENT NOTICE:

  The technology is licensed by the Massachusetts Institute of
  Technology, Rutgers State University of New Jersey, and the Research
  Foundation of State University of New York at Stony Brook under
  United States of America Serial No. 11/760379 and to the patents
  and/or patent applications resulting from it.

PATENT MARKING NOTICE:

  This software is covered by US Patent No. 8,185,551.
  This software is covered by US Patent No. 8,489,638.

PATENT RIGHTS GRANT:

  "THIS IMPLEMENTATION" means the copyrightable works distributed by
  Tokutek as part of the Fractal Tree project.

  "PATENT CLAIMS" means the claims of patents that are owned or
  licensable by Tokutek, both currently or in the future; and that in
  the absence of this license would be infringed by THIS
  IMPLEMENTATION or by using or running THIS IMPLEMENTATION.

  "PATENT CHALLENGE" shall mean a challenge to the validity,
  patentability, enforceability and/or non-infringement of any of the
  PATENT CLAIMS or otherwise opposing any of the PATENT CLAIMS.

  Tokutek hereby grants to you, for the term and geographical scope of
  the PATENT CLAIMS, a non-exclusive, no-charge, royalty-free,
  irrevocable (except as stated in this section) patent license to
  make, have made, use, offer to sell, sell, import, transfer, and
  otherwise run, modify, and propagate the contents of THIS
  IMPLEMENTATION, where such license applies only to the PATENT
  CLAIMS.  This grant does not include claims that would be infringed
  only as a consequence of further modifications of THIS
  IMPLEMENTATION.  If you or your agent or licensee institute or order
  or agree to the institution of patent litigation against any entity
  (including a cross-claim or counterclaim in a lawsuit) alleging that
  THIS IMPLEMENTATION constitutes direct or contributory patent
  infringement, or inducement of patent infringement, then any rights
  granted to you under this License shall terminate as of the date
  such litigation is filed.  If you or your agent or exclusive
  licensee institute or order or agree to the institution of a PATENT
  CHALLENGE, then Tokutek may terminate any rights granted to you
  under this License.
*/

#ident "Copyright (c) 2007-2013 Tokutek Inc.  All rights reserved."
#ident "The technology is licensed by the Massachusetts Institute of Technology, Rutgers State University of New Jersey, and the Research Foundation of State University of New York at Stony Brook under United States of America Serial No. 11/760379 and to the patents and/or patent applications resulting from it."
#ident "$Id$"

#include <db.h>
#include "ydb-internal.h"
#include "indexer.h"
#include <ft/log_header.h>
#include <ft/cachetable/checkpoint.h>
#include "ydb_row_lock.h"
#include "ydb_write.h"
#include "ydb_db.h"
#include <portability/toku_atomic.h>
#include <util/status.h>

static YDB_WRITE_LAYER_STATUS_S ydb_write_layer_status;
#ifdef STATUS_VALUE
#undef STATUS_VALUE
#endif
#define STATUS_VALUE(x) ydb_write_layer_status.status[x].value.num

#define STATUS_INIT(k,c,t,l,inc) TOKUFT_STATUS_INIT(ydb_write_layer_status, k, c, t, l, inc)

static void
ydb_write_layer_status_init (void) {
    // Note, this function initializes the keyname, type, and legend fields.
    // Value fields are initialized to zero by compiler.
    STATUS_INIT(YDB_LAYER_NUM_INSERTS,                nullptr, UINT64,   "dictionary inserts", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_INSERTS_FAIL,           nullptr, UINT64,   "dictionary inserts fail", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_DELETES,                nullptr, UINT64,   "dictionary deletes", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_DELETES_FAIL,           nullptr, UINT64,   "dictionary deletes fail", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_UPDATES,                nullptr, UINT64,   "dictionary updates", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_UPDATES_FAIL,           nullptr, UINT64,   "dictionary updates fail", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_UPDATES_BROADCAST,      nullptr, UINT64,   "dictionary broadcast updates", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_UPDATES_BROADCAST_FAIL, nullptr, UINT64,   "dictionary broadcast updates fail", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_MULTI_INSERTS,          nullptr, UINT64,   "dictionary multi inserts", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_MULTI_INSERTS_FAIL,     nullptr, UINT64,   "dictionary multi inserts fail", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_MULTI_DELETES,          nullptr, UINT64,   "dictionary multi deletes", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_MULTI_DELETES_FAIL,     nullptr, UINT64,   "dictionary multi deletes fail", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_MULTI_UPDATES,          nullptr, UINT64,   "dictionary updates multi", TOKU_ENGINE_STATUS);
    STATUS_INIT(YDB_LAYER_NUM_MULTI_UPDATES_FAIL,     nullptr, UINT64,   "dictionary updates multi fail", TOKU_ENGINE_STATUS);
    ydb_write_layer_status.initialized = true;
}
#undef STATUS_INIT

void
ydb_write_layer_get_status(YDB_WRITE_LAYER_STATUS statp) {
    if (!ydb_write_layer_status.initialized)
        ydb_write_layer_status_init();
    *statp = ydb_write_layer_status;
}


static inline uint32_t 
get_prelocked_flags(uint32_t flags) {
    uint32_t lock_flags = flags & (DB_PRELOCKED | DB_PRELOCKED_WRITE);
    return lock_flags;
}

// these next two static functions are defined
// both here and ydb.c. We should find a good
// place for them.
static int
ydb_getf_do_nothing(DBT const* UU(key), DBT const* UU(val), void* UU(extra)) {
    return 0;
}

// Check if the available file system space is less than the reserve
// Returns ENOSPC if not enough space, othersize 0
static inline int 
env_check_avail_fs_space(DB_ENV *env) {
    int r = env->i->fs_state == FS_RED ? ENOSPC : 0; 
    if (r) {
        env->i->enospc_redzone_ctr++;
    }
    return r;
}

// Return 0 if proposed pair do not violate size constraints of DB
// (insertion is legal)
// Return non zero otherwise.
static int
db_put_check_size_constraints(DB *db, const DBT *key, const DBT *val) {
    int r = 0;
    unsigned int klimit, vlimit;

    toku_ft_get_maximum_advised_key_value_lengths(&klimit, &vlimit);
    if (key->size > klimit) {
        r = toku_ydb_do_error(db->dbenv, EINVAL, 
                "The largest key allowed is %u bytes", klimit);
    } else if (val->size > vlimit) {
        r = toku_ydb_do_error(db->dbenv, EINVAL, 
                "The largest value allowed is %u bytes", vlimit);
    }
    return r;
}

int
toku_db_del(DB *db, DB_TXN *txn, DBT *key, uint32_t flags, bool holds_mo_lock) {
    HANDLE_PANICKED_DB(db);
    HANDLE_DB_ILLEGAL_WORKING_PARENT_TXN(db, txn);
    HANDLE_READ_ONLY_TXN(txn);

    uint32_t unchecked_flags = flags;
    //DB_DELETE_ANY means delete regardless of whether it exists in the db.
    bool error_if_missing = (bool)(!(flags&DB_DELETE_ANY));
    unchecked_flags &= ~DB_DELETE_ANY;
    uint32_t lock_flags = get_prelocked_flags(flags);
    unchecked_flags &= ~lock_flags;
    bool do_locking = (bool)(db->i->dict->get_lt() && !(lock_flags&DB_PRELOCKED_WRITE));

    int r = 0;
    if (unchecked_flags!=0) {
        r = EINVAL;
    }

    if (r == 0 && error_if_missing) {
        //Check if the key exists in the db.
        r = db_getf_set(db, txn, lock_flags|DB_SERIALIZABLE|DB_RMW, key, ydb_getf_do_nothing, NULL);
    }
    if (r == 0 && do_locking) {
        //Do locking if necessary.
        r = toku_db_get_point_write_lock(db, txn, key);
    }
    if (r == 0) {
        DBT ft_key;
        void* data = alloca(sizeof(uint64_t) + key->size);
        db->i->dict->fill_ft_key(key, data, &ft_key);
        //Do the actual deleting.
        if (!holds_mo_lock) toku_multi_operation_client_lock();
        toku_ft_delete(db->i->ft_handle, &ft_key, txn ? db_txn_struct_i(txn)->tokutxn : 0);
        if (!holds_mo_lock) toku_multi_operation_client_unlock();
    }

    if (r == 0) {
        STATUS_VALUE(YDB_LAYER_NUM_DELETES)++;  // accountability 
    }
    else {
        STATUS_VALUE(YDB_LAYER_NUM_DELETES_FAIL)++;  // accountability 
    }
    return r;
}

static int
db_put(DB *db, DB_TXN *txn, DBT *key, DBT *val, int flags, bool do_log) {
    int r = 0;
    bool unique = false;
    enum ft_msg_type type = FT_INSERT;
    if (flags == DB_NOOVERWRITE) {
        unique = true;
    } else if (flags == DB_NOOVERWRITE_NO_ERROR) {
        type = FT_INSERT_NO_OVERWRITE;
    } else if (flags != 0) {
        // All other non-zero flags are unsupported
        r = EINVAL;
    }
    if (r == 0) {
        DBT ft_key;
        void* data = alloca(sizeof(uint64_t) + key->size);
        db->i->dict->fill_ft_key(key, data, &ft_key);
        TOKUTXN ttxn = txn ? db_txn_struct_i(txn)->tokutxn : nullptr;
        if (unique) {
            r = toku_ft_insert_unique(db->i->ft_handle, &ft_key, val, ttxn, do_log);
        } else {
            toku_ft_maybe_insert(db->i->ft_handle, &ft_key, val, ttxn, false, ZERO_LSN, do_log, type);
        }
        invariant(r == DB_KEYEXIST || r == 0);
    }
    return r;
}

int
toku_db_put(DB *db, DB_TXN *txn, DBT *key, DBT *val, uint32_t flags, bool holds_mo_lock) {
    HANDLE_PANICKED_DB(db);
    HANDLE_DB_ILLEGAL_WORKING_PARENT_TXN(db, txn);
    HANDLE_READ_ONLY_TXN(txn);
    int r = 0;

    uint32_t lock_flags = get_prelocked_flags(flags);
    flags &= ~lock_flags;

    r = db_put_check_size_constraints(db, key, val);

    //Do locking if necessary.
    bool do_locking = (bool)(db->i->dict->get_lt() && !(lock_flags&DB_PRELOCKED_WRITE));
    if (r == 0 && do_locking) {
        r = toku_db_get_point_write_lock(db, txn, key);
    }
    if (r == 0) {
        //Insert into the ft.
        if (!holds_mo_lock) toku_multi_operation_client_lock();
        r = db_put(db, txn, key, val, flags, true);
        if (!holds_mo_lock) toku_multi_operation_client_unlock();
    }

    if (r == 0) {
        // helgrind flags a race on this status update.  we increment it atomically to satisfy helgrind.
        // STATUS_VALUE(YDB_LAYER_NUM_INSERTS)++;  // accountability 
        (void) toku_sync_fetch_and_add(&STATUS_VALUE(YDB_LAYER_NUM_INSERTS), 1);
    } else {
        // STATUS_VALUE(YDB_LAYER_NUM_INSERTS_FAIL)++;  // accountability 
        (void) toku_sync_fetch_and_add(&STATUS_VALUE(YDB_LAYER_NUM_INSERTS_FAIL), 1);
    }

    return r;
}

static int
toku_db_update(DB *db, DB_TXN *txn,
               const DBT *key,
               const DBT *update_function_extra,
               uint32_t flags) {
    HANDLE_PANICKED_DB(db);
    HANDLE_DB_ILLEGAL_WORKING_PARENT_TXN(db, txn);
    HANDLE_READ_ONLY_TXN(txn);
    int r = 0;

    uint32_t lock_flags = get_prelocked_flags(flags);
    flags &= ~lock_flags;

    r = db_put_check_size_constraints(db, key, update_function_extra);
    if (r != 0) { goto cleanup; }

    bool do_locking;
    do_locking = (db->i->dict->get_lt() && !(lock_flags & DB_PRELOCKED_WRITE));
    if (do_locking) {
        r = toku_db_get_point_write_lock(db, txn, key);
        if (r != 0) { goto cleanup; }
    }

    {
        TOKUTXN ttxn;
        ttxn = txn ? db_txn_struct_i(txn)->tokutxn : NULL;
        DBT ft_key;
        void* data = alloca(sizeof(uint64_t) + key->size);
        db->i->dict->fill_ft_key(key, data, &ft_key);
        toku_multi_operation_client_lock();
        toku_ft_maybe_update(db->i->ft_handle, &ft_key, update_function_extra, ttxn,
                                  false, ZERO_LSN, true);
        toku_multi_operation_client_unlock();
    }
cleanup:
    if (r == 0) 
        STATUS_VALUE(YDB_LAYER_NUM_UPDATES)++;  // accountability 
    else
        STATUS_VALUE(YDB_LAYER_NUM_UPDATES_FAIL)++;  // accountability 
    return r;
}


// DB_IS_RESETTING_OP is true if the dictionary should be considered as if created by this transaction.
// For example, it will be true if toku_db_update_broadcast() is used to implement a schema change (such
// as adding a column), and will be false if used simply to update all the rows of a table (such as 
// incrementing a field).
static int
toku_db_update_broadcast(DB *db, DB_TXN *txn,
                         const DBT *update_function_extra,
                         uint32_t flags) {
    HANDLE_PANICKED_DB(db);
    HANDLE_DB_ILLEGAL_WORKING_PARENT_TXN(db, txn);
    HANDLE_READ_ONLY_TXN(txn);
    int r = 0;

    uint32_t lock_flags = get_prelocked_flags(flags);
    flags &= ~lock_flags;
    uint32_t is_resetting_op_flag = flags & DB_IS_RESETTING_OP;
    flags &= is_resetting_op_flag;
    bool is_resetting_op = (is_resetting_op_flag != 0);
    

    if (is_resetting_op) {
        if (txn->parent != NULL) {
            r = EINVAL; // cannot have a parent if you are a resetting op
            goto cleanup;
        }
        r = toku_db_pre_acquire_fileops_lock(db, txn);
        if (r != 0) { goto cleanup; }
    }
    {
        DBT null_key;
        toku_init_dbt(&null_key);
        r = db_put_check_size_constraints(db, &null_key, update_function_extra);
        if (r != 0) { goto cleanup; }
    }

    bool do_locking;
    do_locking = (db->i->dict->get_lt() && !(lock_flags & DB_PRELOCKED_WRITE));
    if (do_locking) {
        r = toku_db_pre_acquire_table_lock(db, txn);
        if (r != 0) { goto cleanup; }
    }

    TOKUTXN ttxn;
    ttxn = txn ? db_txn_struct_i(txn)->tokutxn : NULL;
    toku_multi_operation_client_lock();
    assert(false); // need to fix this
    toku_ft_maybe_update_broadcast(db->i->ft_handle, update_function_extra, ttxn,
                                        false, ZERO_LSN, true, is_resetting_op);
    toku_multi_operation_client_unlock();

cleanup:
    if (r == 0) 
        STATUS_VALUE(YDB_LAYER_NUM_UPDATES_BROADCAST)++;  // accountability 
    else
        STATUS_VALUE(YDB_LAYER_NUM_UPDATES_BROADCAST_FAIL)++;  // accountability 
    return r;
}

static uint32_t 
lookup_src_db(uint32_t num_dbs, DB *db_array[], DB *src_db) {
    uint32_t which_db;
    for (which_db = 0; which_db < num_dbs; which_db++) 
        if (db_array[which_db] == src_db)
            break;
    return which_db;
}

//
// if a hot index is in progress, gets the indexer
// also verifies that there is at most one hot index
// in progress. If it finds more than one, then returns EINVAL
// 
static int
get_indexer_if_exists(
    uint32_t num_dbs, 
    DB **db_array, 
    DB *src_db,
    DB_INDEXER** indexerp,
    bool *src_db_is_indexer_src
    ) 
{
    int r = 0;
    DB_INDEXER* first_indexer = NULL;
    for (uint32_t i = 0; i < num_dbs; i++) {
        DB_INDEXER* indexer = toku_db_get_indexer(db_array[i]);
        if (indexer) {
            if (!first_indexer) {
                first_indexer = indexer;
            }
            else if (first_indexer != indexer) {
                r = EINVAL;
            }
        }
    }
    if (r == 0) {
        if (first_indexer) {
            DB* indexer_src_db = toku_indexer_get_src_db(first_indexer);
            // we should just make this an invariant
            if (src_db == indexer_src_db) {
                *src_db_is_indexer_src = true;
            }
        }
        *indexerp = first_indexer;
    }
    return r;
}

// Requires: If remaining_flags is non-null, this function performs any required uniqueness checks
//           Otherwise, the caller is responsible.
static int
do_put_multiple(DB_TXN *txn, uint32_t num_dbs, DB *db_array[], DBT_ARRAY keys[], DBT_ARRAY vals[], uint32_t *remaining_flags, DB *src_db, const DBT *src_key, bool indexer_shortcut) {
    int r = 0;
    for (uint32_t which_db = 0; which_db < num_dbs; which_db++) {
        DB *db = db_array[which_db];

        invariant(keys[which_db].size == vals[which_db].size);
        paranoid_invariant(keys[which_db].size <= keys[which_db].capacity);
        paranoid_invariant(vals[which_db].size <= vals[which_db].capacity);

        if (keys[which_db].size > 0) {
            bool do_put = true;
            DB_INDEXER *indexer = toku_db_get_indexer(db);
            if (indexer && !indexer_shortcut) { // if this db is the index under construction
                DB *indexer_src_db = toku_indexer_get_src_db(indexer);
                invariant(indexer_src_db != NULL);
                const DBT *indexer_src_key;
                if (src_db == indexer_src_db)
                    indexer_src_key = src_key;
                else {
                    uint32_t which_src_db = lookup_src_db(num_dbs, db_array, indexer_src_db);
                    invariant(which_src_db < num_dbs);
                    // The indexer src db must have exactly one item or we don't know how to continue.
                    invariant(keys[which_src_db].size == 1);
                    indexer_src_key = &keys[which_src_db].dbts[0];
                }
                do_put = toku_indexer_should_insert_key(indexer, indexer_src_key);
                toku_indexer_update_estimate(indexer);
            }
            if (do_put) {
                for (uint32_t i = 0; i < keys[which_db].size; i++) {
                    int flags = 0;
                    if (remaining_flags != nullptr) {
                        flags = remaining_flags[which_db];
                        invariant(!(flags & DB_NOOVERWRITE_NO_ERROR));
                    }
                    r = db_put(db, txn, &keys[which_db].dbts[i], &vals[which_db].dbts[i], flags, true);
                    if (r != 0) {
                        goto done;
                    }
                }
            }
        }
    }
done:
    return r;
}

static int
env_put_multiple_internal(
    DB_ENV *env, 
    DB *src_db, 
    DB_TXN *txn, 
    const DBT *src_key, 
    const DBT *src_val, 
    uint32_t num_dbs, 
    DB **db_array, 
    DBT_ARRAY *keys,
    DBT_ARRAY *vals,
    uint32_t *flags_array) 
{
    int r;
    DBT_ARRAY put_keys[num_dbs];
    DBT_ARRAY put_vals[num_dbs];
    DB_INDEXER* indexer = NULL;

    HANDLE_PANICKED_ENV(env);
    HANDLE_READ_ONLY_TXN(txn);

    uint32_t lock_flags[num_dbs];
    uint32_t remaining_flags[num_dbs];
    bool indexer_shortcut = false;
    bool indexer_lock_taken = false;
    bool src_same = false;

    if (!txn || !num_dbs) {
        r = EINVAL;
        goto cleanup;
    }
    if (!env->i->generate_row_for_put) {
        r = EINVAL;
        goto cleanup;
    }

    HANDLE_ILLEGAL_WORKING_PARENT_TXN(env, txn);
    r = get_indexer_if_exists(num_dbs, db_array, src_db, &indexer, &src_same);
    if (r) {
        goto cleanup;
    }

    for (uint32_t which_db = 0; which_db < num_dbs; which_db++) {
        DB *db = db_array[which_db];

        lock_flags[which_db] = get_prelocked_flags(flags_array[which_db]);
        remaining_flags[which_db] = flags_array[which_db] & ~lock_flags[which_db];

        //Generate the row
        if (db == src_db) {
            put_keys[which_db].size = put_keys[which_db].capacity = 1;
            put_keys[which_db].dbts = const_cast<DBT*>(src_key);

            put_vals[which_db].size = put_vals[which_db].capacity = 1;
            put_vals[which_db].dbts = const_cast<DBT*>(src_val);
        }
        else {
            r = env->i->generate_row_for_put(db, src_db, &keys[which_db], &vals[which_db], src_key, src_val);
            if (r != 0) goto cleanup;

            paranoid_invariant(keys[which_db].size <= keys[which_db].capacity);
            paranoid_invariant(vals[which_db].size <= vals[which_db].capacity);
            paranoid_invariant(keys[which_db].size == vals[which_db].size);

            put_keys[which_db] = keys[which_db];
            put_vals[which_db] = vals[which_db];
        }
        for (uint32_t i = 0; i < put_keys[which_db].size; i++) {
            DBT &put_key = put_keys[which_db].dbts[i];
            DBT &put_val = put_vals[which_db].dbts[i];

            // check size constraints
            r = db_put_check_size_constraints(db, &put_key, &put_val);
            if (r != 0) goto cleanup;

            if (remaining_flags[which_db] == DB_NOOVERWRITE_NO_ERROR) {
                //put_multiple does not support delaying the no error, since we would
                //have to log the flag in the put_multiple.
                r = EINVAL; goto cleanup;
            }

            //Do locking if necessary.
            if (db->i->dict->get_lt() && !(lock_flags[which_db] & DB_PRELOCKED_WRITE)) {
                //Needs locking
                r = toku_db_get_point_write_lock(db, txn, &put_key);
                if (r != 0) goto cleanup;
            }
        }
    }
    
    if (indexer) {
        // do a cheap check
        if (src_same) {
            bool may_insert = toku_indexer_may_insert(indexer, src_key);
            if (!may_insert) {
                toku_indexer_lock(indexer);
                indexer_lock_taken = true;
            }
            else {
                indexer_shortcut = true;
            }
        }
    }
    toku_multi_operation_client_lock();
    r = do_put_multiple(txn, num_dbs, db_array, put_keys, put_vals, remaining_flags, src_db, src_key, indexer_shortcut);
    toku_multi_operation_client_unlock();
    if (indexer_lock_taken) {
        toku_indexer_unlock(indexer);
    }

cleanup:
    if (r == 0)
        STATUS_VALUE(YDB_LAYER_NUM_MULTI_INSERTS) += num_dbs;  // accountability 
    else
        STATUS_VALUE(YDB_LAYER_NUM_MULTI_INSERTS_FAIL) += num_dbs;  // accountability 
    return r;
}

int 
autotxn_db_del(DB* db, DB_TXN* txn, DBT* key, uint32_t flags) {
    bool changed; int r;
    r = toku_db_construct_autotxn(db, &txn, &changed, false);
    if (r!=0) return r;
    r = toku_db_del(db, txn, key, flags, false);
    return toku_db_destruct_autotxn(txn, r, changed);
}

int 
autotxn_db_put(DB* db, DB_TXN* txn, DBT* key, DBT* data, uint32_t flags) {
    //{ unsigned i; printf("put %p keylen=%d key={", db, key->size); for(i=0; i<key->size; i++) printf("%d,", ((char*)key->data)[i]); printf("} datalen=%d data={", data->size); for(i=0; i<data->size; i++) printf("%d,", ((char*)data->data)[i]); printf("}\n"); }
    bool changed; int r;
    r = env_check_avail_fs_space(db->dbenv);
    if (r != 0) { goto cleanup; }
    r = toku_db_construct_autotxn(db, &txn, &changed, false);
    if (r!=0) {
        goto cleanup;
    }
    r = toku_db_put(db, txn, key, data, flags, false);
    r = toku_db_destruct_autotxn(txn, r, changed);
cleanup:
    return r;
}

int
autotxn_db_update(DB *db, DB_TXN *txn,
                  const DBT *key,
                  const DBT *update_function_extra,
                  uint32_t flags) {
    bool changed; int r;
    r = env_check_avail_fs_space(db->dbenv);
    if (r != 0) { goto cleanup; }
    r = toku_db_construct_autotxn(db, &txn, &changed, false);
    if (r != 0) { return r; }
    r = toku_db_update(db, txn, key, update_function_extra, flags);
    r = toku_db_destruct_autotxn(txn, r, changed);
cleanup:
    return r;
}

int
autotxn_db_update_broadcast(DB *db, DB_TXN *txn,
                            const DBT *update_function_extra,
                            uint32_t flags) {
    bool changed; int r;
    r = env_check_avail_fs_space(db->dbenv);
    if (r != 0) { goto cleanup; }
    r = toku_db_construct_autotxn(db, &txn, &changed, false);
    if (r != 0) { return r; }
    r = toku_db_update_broadcast(db, txn, update_function_extra, flags);
    r = toku_db_destruct_autotxn(txn, r, changed);
cleanup:
    return r;
}

int
env_put_multiple(DB_ENV *env, DB *src_db, DB_TXN *txn, const DBT *src_key, const DBT *src_val, uint32_t num_dbs, DB **db_array, DBT_ARRAY *keys, DBT_ARRAY *vals, uint32_t *flags_array) {
    int r = env_check_avail_fs_space(env);
    if (r == 0) {
        r = env_put_multiple_internal(env, src_db, txn, src_key, src_val, num_dbs, db_array, keys, vals, flags_array);
    }
    return r;
}

int
toku_ydb_check_avail_fs_space(DB_ENV *env) {
    int rval = env_check_avail_fs_space(env);
    return rval;
}
#undef STATUS_VALUE

#include <toku_race_tools.h>
void __attribute__((constructor)) toku_ydb_write_helgrind_ignore(void);
void
toku_ydb_write_helgrind_ignore(void) {
    TOKU_VALGRIND_HG_DISABLE_CHECKING(&ydb_write_layer_status, sizeof ydb_write_layer_status);
}
