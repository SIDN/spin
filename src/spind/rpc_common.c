#include "spindata.h"
#include "tree.h"
#include "rpc_common.h"

#include "spin_log.h"

typedef struct rpc_data {
    rpc_func_p rpcd_func;
    void *rpcd_cb;
    int rpcd_nargs;
    rpc_arg_desc_t *rpcd_args;
    rpc_argtype rpcd_result_type;
} rpc_data_t;

static tree_t *rpcfunctree;

void
rpc_register(char *name, rpc_func_p func, void *cb, int nargs, rpc_arg_desc_t *args, rpc_argtype result_type) {
    rpc_data_t rd;

    rd.rpcd_func = func;
    rd.rpcd_nargs = nargs;
    rd.rpcd_args = args;
    rd.rpcd_result_type = result_type;

    if (rpcfunctree == NULL) {
        // First call, make tree

        rpcfunctree = tree_create(cmp_strs);
    }

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
    int argok;
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

    for (call_arg=0; call_arg<nargs; call_arg++) {
        rpca = args + call_arg;
        argok = 0;

        // Find argument and check type in function definition
        // Arguments can be in wrong order

        for (def_arg=0; def_arg<funcnargs; def_arg++) {
            rpcd = funcargs + def_arg;
            if (strcmp(rpca->rpc_desc.rpca_name, rpcd->rpca_name) == 0 &&
                            rpca->rpc_desc.rpca_type == rpcd->rpca_type) {
                    argumentvals[def_arg] = rpca->rpc_val;
                    argok = 1;
                    break;
            }
        }

        if (!argok) {
            spin_log(LOG_ERR, "Argument %s unknown or wrong type\n", rpca->rpc_desc.rpca_name);
            result->rpc_desc.rpca_name = "error";
            result->rpc_desc.rpca_type = RPCAT_STRING;
            result->rpc_val.rpca_svalue = "No such argument registered";

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

    // Do something with res
    return res;
}

rpc_arg_desc_t tf_args[] = {
    { "arg1", RPCAT_INT },
    { "arg2", RPCAT_STRING },
};

static int
testfunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    static char buf[100];

    fprintf(stderr, "testfunc called\n");
    sprintf(buf, "Int:%d, String: %s", args[0].rpca_ivalue,args[1].rpca_svalue);
    result->rpca_svalue = buf;
    return 0;
}

static int
devlistfunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    spin_data jsonrpc_devices(spin_data arg);

    fprintf(stderr, "devlistfunc called");
    result->rpca_cvalue = jsonrpc_devices(NULL);
    return 0;
}

void
init_rpc_common() {

    rpc_register("testfunc", testfunc, (void *) 0, 2, tf_args, RPCAT_STRING);
    rpc_register("devicelist", devlistfunc, (void *) 0, 0, NULL, RPCAT_COMPLEX);
}
