#include "spindata.h"
#include "tree.h"
#include "rpc_common.h"
#include "rpc_calls.h"

#include "spin_log.h"

/*
 * Administration for an RPC
 */
typedef struct rpc_data {
    rpc_func_p      rpcd_func;
    void *          rpcd_cb;
    int             rpcd_nargs;
    rpc_arg_desc_t *rpcd_args;
    rpc_argtype     rpcd_result_type;
} rpc_data_t;

static tree_t *rpcfunctree = NULL;

static char * rpcatype(int t) {

    switch (t) {
    case RPCAT_INT:
        return "integer";
    case RPCAT_STRING:
        return "string";
    case RPCAT_COMPLEX:
    default:
        return "complex";
    }
}

/*
 * Lists the registered RPC procedures and their arguments
 */
static int
rpc_list_registered_procedures(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    tree_t * rpctree;
    tree_entry_t* cur;
    cJSON *arobj, *argarobj, *argobj, *rpcfuncobj;
    rpc_data_t *rdp;
    rpc_arg_desc_t *radp;
    int i;

    rpctree = (tree_t *) cb;
    printf("[XX] 2FUNCTREE AT %p\n", rpctree);

    arobj = cJSON_CreateArray();
    cur = tree_first(rpctree);
    while (cur != NULL) {
        rpcfuncobj = cJSON_CreateObject();

        cJSON_AddStringToObject(rpcfuncobj, "rpc-name", (char *) cur->key);
        argarobj = cJSON_CreateArray();
        rdp = (rpc_data_t *) cur->data;
        for (i=0; i<rdp->rpcd_nargs; i++) {
            radp = &rdp->rpcd_args[i];
            argobj = cJSON_CreateObject();
            cJSON_AddStringToObject(argobj, "rpc-arg-name", radp->rpca_name);
            cJSON_AddStringToObject(argobj, "rpc-arg-type", rpcatype(radp->rpca_type));

            cJSON_AddItemToArray(argarobj, argobj);
        }
        cJSON_AddItemToObject(rpcfuncobj, "rpc-args", argarobj);
        cJSON_AddStringToObject(rpcfuncobj, "rpc-result-type", rpcatype(rdp->rpcd_result_type));

        cJSON_AddItemToArray(arobj, rpcfuncobj);
        cur = tree_next(cur);
    }

    result->rpca_cvalue = arobj;
    return 0;
}

void
rpc_register(char *name, rpc_func_p func, void *cb, int nargs, rpc_arg_desc_t *args, rpc_argtype result_type) {
    rpc_data_t rd;

    if (rpcfunctree == NULL) {
        // First call, make tree, this is essentially a singleton pattern
        rpcfunctree = tree_create(cmp_strs);

        // Immediately register one rpc method; the one that lists all
        // registered methods
        rpc_register("list_rpc_methods", rpc_list_registered_procedures, rpcfunctree, 0, 0, RPCAT_COMPLEX);
    }

    rd.rpcd_func = func;
    rd.rpcd_cb = cb;
    rd.rpcd_nargs = nargs;
    rd.rpcd_args = args;
    rd.rpcd_result_type = result_type;

    tree_add(rpcfunctree, strlen(name)+1, name, sizeof(rd), &rd, 1);
}

int
rpc_call(char *name, int nargs, rpc_arg_t *args, rpc_arg_t *result) {
    rpc_data_t *rdp;
    tree_entry_t *funcleaf;
    int funcnargs;
    rpc_arg_desc_t *funcargs, *rpcd;
    rpc_arg_t *rpca;
    int call_arg, def_arg;
    int res;
    rpc_arg_val_t *argumentvals;

    funcleaf = tree_find(rpcfunctree, strlen(name)+1, name);
    if (funcleaf == NULL) {
        spin_log(LOG_ERR, "No function %s registered\n", name);
        result->rpc_desc.rpca_name = "error";
        result->rpc_desc.rpca_type = RPCAT_STRING;
        result->rpc_val.rpca_svalue = "No such function registered";
        return -1;
    }
    rdp = (rpc_data_t *) funcleaf->data;
    funcnargs = rdp->rpcd_nargs;
    funcargs = rdp->rpcd_args;
    argumentvals = (rpc_arg_val_t *) malloc(funcnargs*sizeof(rpc_arg_val_t));

    if (nargs != funcnargs) {
        spin_log(LOG_ERR, "Wrong # of args for func %s\n", name);
        result->rpc_desc.rpca_name = "error";
        result->rpc_desc.rpca_type = RPCAT_STRING;
        result->rpc_val.rpca_svalue = "Wrong number of arguments";

        free(argumentvals);
        return -1;
    }

    for (call_arg=0; call_arg<nargs; call_arg++) {
        int argnameok, argtypeok;

        rpca = args + call_arg;
        argnameok = 0;
        argtypeok = 0;

        // Find argument and check type in function definition
        // Arguments can be in wrong order

        for (def_arg=0; def_arg<funcnargs; def_arg++) {
            rpcd = funcargs + def_arg;
            argnameok = strcmp(rpca->rpc_desc.rpca_name, rpcd->rpca_name) == 0;
            argtypeok = rpca->rpc_desc.rpca_type == rpcd->rpca_type;
            if (argnameok && argtypeok) {
                    argumentvals[def_arg] = rpca->rpc_val;
                    break;
            }
        }

        if (argnameok == 0 || argtypeok == 0) {
            spin_log(LOG_ERR, "Argument %s unknown or wrong type\n", rpca->rpc_desc.rpca_name);
            result->rpc_desc.rpca_name = "error";
            result->rpc_desc.rpca_type = RPCAT_STRING;
            result->rpc_val.rpca_svalue = argnameok == 0 ? "No such argument registered" : "Wrong type of argument";

            free(argumentvals);
            return -1;
        }
    }

    // Ok, arguments parsed and filled in
    // Let's make the call

    result->rpc_desc.rpca_name = "unused";
    result->rpc_desc.rpca_type = rdp->rpcd_result_type;
    res = (*rdp->rpcd_func)(rdp->rpcd_cb, argumentvals, &result->rpc_val);

    free(argumentvals);

    return res;
}

void
register_internal_functions() {
    //rpc_register("list_rpc_methods", rpc_list_registered_procedures, rpcfunctree, 0, 0, RPCAT_COMPLEX);
}

void rpc_cleanup() {
    cleanup_rpcs();
    tree_destroy(rpcfunctree);
}
