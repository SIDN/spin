# RPC binding in Spind

Currently to implement an RPC in Spind it needs some small amount of code in rpc_json.c(and/or rpc_ubus.c) to get parameters of RPC and to call the actual work routine, and of course the work routine itself.

Perhaps it is a good idea to let the module of the work routine register the routine to the RPC layer. This makes it possible to implement a varying set of RPC's depending on which code is running in spind.

If we do that the description of parameters and result might need to be included. Currently we use a cJSON (nicknamed spin_data) struct both as input and as output.

## Current code

This is now implemented, file rpc_common.h



	typedef enum {
	    RPCAT_INT,
	    RPCAT_STRING,
	    RPCAT_COMPLEX
	} rpc_argtype;
	
	typedef struct {
	    char *      rpca_name;
	    rpc_argtype rpca_type;
	} rpc_arg_desc_t;
	
	typedef union {
	    int         rpca_ivalue;
	    char *      rpca_svalue;
	    spin_data   rpca_cvalue;
	} rpc_arg_val_t;
	
	typedef struct {
	    rpc_arg_desc_t  rpc_desc;
	    rpc_arg_val_t   rpc_val;
	} rpc_arg_t;
	
	typedef int (*rpc_func_p)(rpc_arg_val_t *args, rpc_arg_val_t *result);
	
	void rpc_register(char *name, rpc_func_p func, int nargs, rpc_arg_desc_t *args, rpc_argtype result_type);
	
Implement your to be called function as the type *rpc_func_p* and call rpc_register to set it up.

Implementation(for now just JSON-RPC) will call the function with the correct arguments if a call has been made, and send the return value back.

A test example:

	rpc_arg_desc_t tf_args[] = {
	    { "arg1", RPCAT_INT },
	    { "arg2", RPCAT_STRING },
	};
	
	static int
	testfunc(rpc_arg_val_t *args, rpc_arg_val_t *result) {
	    static char buf[100];
	
	    sprintf(buf, "Int:%d, String: %s", args[0].rpca_ivalue,args[1].rpca_svalue);
	    result->rpca_svalue = buf;
	    return 0;
	}
	
	// to be registered by the following call
	
	rpc_register("testfunc", testfunc, 2, tf_args, RPCAT_STRING);

	
## Ubus

It is not so easy with Ubus. Currently all methods of Ubus are initialized at the same time, and you cannot add methods on the fly.

So if you want to have the methods spin.spind.get_devices and spin.spind.blockflow both implemented you have to set them up together and make one call to initialize the spin.spind object.

There are two ways to implement the same RPC binding mechanism as described above:
- First do all the rpc_register calls for all the implemented RPC's, and after that have the common code call Ubus to set up the reception of the RPC's. This will mean another carefully placed init-call.
- Make one Ubus method, spin.spind.rpc and when that is invoked get the real method and all the arguments from the JSON passed to that spin.spind.rpc method.

This needs discussion.
