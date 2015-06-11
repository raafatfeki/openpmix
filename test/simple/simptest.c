/*
 * Copyright (c) 2004-2010 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2011 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2006-2013 Los Alamos National Security, LLC.
 *                         All rights reserved.
 * Copyright (c) 2009-2012 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2011      Oak Ridge National Labs.  All rights reserved.
 * Copyright (c) 2013-2015 Intel, Inc.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 */

#include "src/include/pmix_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "src/api/pmix_common.h"
#include "src/api/pmix_server.h"
#include "src/util/pmix_environ.h"
#include "src/util/output.h"
#include "src/util/printf.h"
#include "src/util/argv.h"
#include "src/buffer_ops/buffer_ops.h"

static int finalized(const char nspace[], int rank, void *server_object,
                     pmix_op_cbfunc_t cbfunc, void *cbdata);
static int abort_fn(const char nspace[], int rank,
                    void *server_object,
                    int status, const char msg[],
                    pmix_proc_t procs[], size_t nprocs,
                    pmix_op_cbfunc_t cbfunc, void *cbdata);
static int fencenb_fn(const pmix_proc_t procs[], size_t nprocs,
                      char *data, size_t ndata,
                      pmix_modex_cbfunc_t cbfunc, void *cbdata);
static int dmodex_fn(const char nspace[], int rank,
                     pmix_modex_cbfunc_t cbfunc, void *cbdata);
static int publish_fn(const char nspace[], int rank,
                      pmix_scope_t scope, pmix_persistence_t persist,
                      const pmix_info_t info[], size_t ninfo,
                      pmix_op_cbfunc_t cbfunc, void *cbdata);
static int lookup_fn(pmix_scope_t scope, int wait, char **keys,
                     pmix_lookup_cbfunc_t cbfunc, void *cbdata);
static int unpublish_fn(pmix_scope_t scope, char **keys,
                        pmix_op_cbfunc_t cbfunc, void *cbdata);
static int spawn_fn(const pmix_app_t apps[], size_t napps,
                    pmix_spawn_cbfunc_t cbfunc, void *cbdata);
static int connect_fn(const pmix_proc_t procs[], size_t nprocs,
                      pmix_op_cbfunc_t cbfunc, void *cbdata);
static int disconnect_fn(const pmix_proc_t procs[], size_t nprocs,
                         pmix_op_cbfunc_t cbfunc, void *cbdata);
static int listener_fn(int listening_sd,
                       pmix_connection_cbfunc_t cbfunc);

static pmix_server_module_t mymodule = {
    finalized,
    abort_fn,
    fencenb_fn,
    dmodex_fn,
    publish_fn,
    lookup_fn,
    unpublish_fn,
    spawn_fn,
    connect_fn,
    disconnect_fn,
    NULL
};

typedef struct {
    pmix_list_item_t super;
    pmix_pdata_t pdata;
} pmix_locdat_t;
PMIX_CLASS_INSTANCE(pmix_locdat_t,
                    pmix_list_item_t,
                    NULL, NULL);

static volatile int wakeup;
static pmix_list_t pubdata;

static void set_namespace(int nprocs, char *ranks, char *nspace);
static void errhandler(pmix_status_t status,
                       pmix_proc_t procs[], size_t nprocs,
                       pmix_info_t info[], size_t ninfo);

int main(int argc, char **argv)
{
    char **client_env=NULL;
    char **client_argv=NULL;
    char *tmp, **atmp, *executable=NULL;
    int rc, nprocs=1, n;
    uid_t myuid;
    gid_t mygid;
    pid_t pid;
    
    /* smoke test */
    if (PMIX_SUCCESS != 0) {
        fprintf(stderr, "ERROR IN COMPUTING CONSTANTS: PMIX_SUCCESS = %d\n", PMIX_SUCCESS);
        exit(1);
    }

    fprintf(stderr, "Testing version %s\n", PMIx_Get_version());

    /* setup the server library */
    if (PMIX_SUCCESS != (rc = PMIx_server_init(&mymodule, true))) {
        fprintf(stderr, "Init failed with error %d\n", rc);
        return rc;
    }
    /* register the errhandler */
    PMIx_Register_errhandler(errhandler);

    /* setup the pub data, in case it is used */
    PMIX_CONSTRUCT(&pubdata, pmix_list_t);
    
    /* see if we were passed the number of procs to run */
    for (n=1; n < argc; n++) {
        if (0 == strcmp("-n", argv[n])) {
            nprocs = strtol(argv[n+1], NULL, 10);
            ++n;  // step over the argument
        } else if (0 == strcmp("-e", argv[n])) {
            executable = strdup(argv[n+1]);
            ++n;
        }
    }
    if (NULL == executable) {
        executable = strdup("simpclient");
    }
    
     /* set common argv and env */
    client_env = pmix_argv_copy(environ);
    pmix_argv_append_nosize(&client_argv, executable);
    
   /* we have a single namespace for all clients */
    atmp = NULL;
    for (n=0; n < nprocs; n++) {
        asprintf(&tmp, "%d", n);
        pmix_argv_append_nosize(&atmp, tmp);
        free(tmp);
    }
    tmp = pmix_argv_join(atmp, ',');
    set_namespace(nprocs, tmp, "foobar");
    free(tmp);
    wakeup = nprocs;
    
    myuid = getuid();
    mygid = getgid();

    /* fork/exec the test */
    for (n = 0; n < nprocs; n++) {
        if (PMIX_SUCCESS != (rc = PMIx_server_setup_fork("foobar", n, &client_env))) {//n
            fprintf(stderr, "Server fork setup failed with error %d\n", rc);
            PMIx_server_finalize();
            return rc;
        }
        if (PMIX_SUCCESS != (rc = PMIx_server_register_client("foobar", n, myuid, mygid, NULL))) {
            fprintf(stderr, "Server fork setup failed with error %d\n", rc);
            PMIx_server_finalize();
            return rc;
        }
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "Fork failed\n");
            PMIx_server_finalize();
            return -1;
        }

        if (pid == 0) {
            execve(executable, client_argv, client_env);
            /* Does not return */
            exit(0);
        }
    }
    
    /* hang around until the client(s) finalize */
    while (0 < wakeup) {
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 100000;
        nanosleep(&ts, NULL);
    }
    
    pmix_argv_free(client_argv);
    pmix_argv_free(client_env);

    /* deregister the errhandler */
    PMIx_Deregister_errhandler();

    /* release any pub data */
    PMIX_LIST_DESTRUCT(&pubdata);
    
    /* finalize the server library */
    if (PMIX_SUCCESS != (rc = PMIx_server_finalize())) {
        fprintf(stderr, "Finalize failed with error %d\n", rc);
    }

    fprintf(stderr, "Test finished OK!\n");

    return rc;
}

static void set_namespace(int nprocs, char *ranks, char *nspace)
{
    size_t ninfo = 6;
    pmix_info_t *info;
    char *regex, *ppn;
    char hostname[1024];

    gethostname(hostname, 1024);
    
    PMIX_INFO_CREATE(info, ninfo);
    (void)strncpy(info[0].key, PMIX_UNIV_SIZE, PMIX_MAX_KEYLEN);
    info[0].value.type = PMIX_UINT32;
    info[0].value.data.uint32 = nprocs;
    
    (void)strncpy(info[1].key, PMIX_SPAWNED, PMIX_MAX_KEYLEN);
    info[1].value.type = PMIX_UINT32;
    info[1].value.data.uint32 = 0;

    (void)strncpy(info[2].key, PMIX_LOCAL_SIZE, PMIX_MAX_KEYLEN);
    info[2].value.type = PMIX_UINT32;
    info[2].value.data.uint32 = nprocs;

    (void)strncpy(info[3].key, PMIX_LOCAL_PEERS, PMIX_MAX_KEYLEN);
    info[3].value.type = PMIX_STRING;
    info[3].value.data.string = strdup(ranks);

    PMIx_generate_regex(hostname, &regex);
    (void)strncpy(info[4].key, PMIX_NODE_MAP, PMIX_MAX_KEYLEN);
    info[4].value.type = PMIX_STRING;
    info[4].value.data.string = regex;
    
    PMIx_generate_ppn(ranks, &ppn);
    (void)strncpy(info[5].key, PMIX_PROC_MAP, PMIX_MAX_KEYLEN);
    info[5].value.type = PMIX_STRING;
    info[5].value.data.string = ppn;

    PMIx_server_register_nspace(nspace, nprocs, info, ninfo);
    PMIX_INFO_FREE(info, ninfo);
}

static void errhandler(pmix_status_t status,
                       pmix_proc_t procs[], size_t nprocs,
                       pmix_info_t info[], size_t ninfo)
{
}

static int finalized(const char nspace[], int rank, void *server_object,
                     pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    --wakeup;
    /* ensure we call the cbfunc so the proc can exit! */
    if (NULL != cbfunc) {
        cbfunc(PMIX_SUCCESS, cbdata);
    }
    return PMIX_SUCCESS;
}

static int abort_fn(const char nspace[], int rank,
                    void *server_object,
                    int status, const char msg[],
                    pmix_proc_t procs[], size_t nprocs,
                    pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    return PMIX_SUCCESS;
}


static int fencenb_fn(const pmix_proc_t procs[], size_t nprocs,
                      char *data, size_t ndata,
                      pmix_modex_cbfunc_t cbfunc, void *cbdata)
{
    pmix_output(0, "SERVER: FENCENB");
    /* pass the provided data back to each participating proc */
    if (NULL != cbfunc) {
        cbfunc(PMIX_SUCCESS, data, ndata, cbdata);
    }
    return PMIX_SUCCESS;
}


static int dmodex_fn(const char nspace[], int rank,
                     pmix_modex_cbfunc_t cbfunc, void *cbdata)
{
    return PMIX_SUCCESS;
}


static int publish_fn(const char nspace[], int rank,
                      pmix_scope_t scope, pmix_persistence_t persist,
                      const pmix_info_t info[], size_t ninfo,
                      pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    pmix_locdat_t *p;
    size_t n;
    
    pmix_output(0, "SERVER: PUBLISH");

    for (n=0; n < ninfo; n++) {
        p = PMIX_NEW(pmix_locdat_t);
        (void)strncpy(p->pdata.proc.nspace, nspace, PMIX_MAX_NSLEN);
        p->pdata.proc.rank = rank;
        (void)strncpy(p->pdata.key, info[n].key, PMIX_MAX_KEYLEN);
        pmix_value_xfer(&p->pdata.value, (pmix_value_t*)&info[n].value);
        pmix_list_append(&pubdata, &p->super);
    }
    if (NULL != cbfunc) {
        cbfunc(PMIX_SUCCESS, cbdata);
    }
    return PMIX_SUCCESS;
}


static int lookup_fn(pmix_scope_t scope, int wait, char **keys,
                     pmix_lookup_cbfunc_t cbfunc, void *cbdata)
{
    pmix_locdat_t *p, *p2;
    pmix_list_t results;
    size_t i, n;
    pmix_pdata_t *pd;
    int ret=PMIX_ERR_NOT_FOUND;
    
    pmix_output(0, "SERVER: LOOKUP");

    PMIX_CONSTRUCT(&results, pmix_list_t);
    
    for (n=0; NULL != keys[n]; n++) {
        PMIX_LIST_FOREACH(p, &pubdata, pmix_locdat_t) {
            if (0 == strncmp(keys[n], p->pdata.key, PMIX_MAX_KEYLEN)) {
                p2 = PMIX_NEW(pmix_locdat_t);
                (void)strncpy(p2->pdata.proc.nspace, p->pdata.proc.nspace, PMIX_MAX_NSLEN);
                p2->pdata.proc.rank = p->pdata.proc.rank;
                (void)strncpy(p2->pdata.key, p->pdata.key, PMIX_MAX_KEYLEN);
                pmix_value_xfer(&p2->pdata.value, &p->pdata.value);
                pmix_list_append(&results, &p2->super);
                break;
            }
        }
    }
    if (0 < (n = pmix_list_get_size(&results))) {
        ret = PMIX_SUCCESS;
        PMIX_PDATA_CREATE(pd, n);
        for (i=0; i < n; i++) {
            p = (pmix_locdat_t*)pmix_list_remove_first(&results);
            (void)strncpy(pd[i].proc.nspace, p->pdata.proc.nspace, PMIX_MAX_NSLEN);
            pd[i].proc.rank = p->pdata.proc.rank;
            (void)strncpy(pd[i].key, p->pdata.key, PMIX_MAX_KEYLEN);
            pmix_value_xfer(&pd[i].value, &p->pdata.value);
        }
    }
    PMIX_LIST_DESTRUCT(&results);
    if (NULL != cbfunc) {
        cbfunc(ret, pd, n, cbdata);
    }
    if (0 < n) {
        PMIX_PDATA_FREE(pd, n);
    }
    return PMIX_SUCCESS;
}


static int unpublish_fn(pmix_scope_t scope, char **keys,
                        pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    pmix_locdat_t *p, *p2;
    size_t n;
    
    pmix_output(0, "SERVER: UNPUBLISH");
    
    for (n=0; NULL != keys[n]; n++) {
        PMIX_LIST_FOREACH_SAFE(p, p2, &pubdata, pmix_locdat_t) {
            if (0 == strncmp(keys[n], p->pdata.key, PMIX_MAX_KEYLEN)) {
                pmix_list_remove_item(&pubdata, &p->super);
                PMIX_RELEASE(p);
                break;
            }
        }
    }
    if (NULL != cbfunc) {
        cbfunc(PMIX_SUCCESS, cbdata);
    }
    return PMIX_SUCCESS;
}


static int spawn_fn(const pmix_app_t apps[], size_t napps,
                    pmix_spawn_cbfunc_t cbfunc, void *cbdata)
{
    return PMIX_SUCCESS;
}


static int connect_fn(const pmix_proc_t procs[], size_t nprocs,
                      pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    return PMIX_SUCCESS;
}


static int disconnect_fn(const pmix_proc_t procs[], size_t nprocs,
                         pmix_op_cbfunc_t cbfunc, void *cbdata)
{
    return PMIX_SUCCESS;
}


static int listener_fn(int listening_sd,
                       pmix_connection_cbfunc_t cbfunc)
{
    return PMIX_SUCCESS;
}


