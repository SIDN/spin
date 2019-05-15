# RPC binding in Spind

Currently to implement an RPC in Spind it needs some small amount of code in rpc_json.c(and/or rpc_ubus.c) to get parameters of RPC and to call the actual work routine, and of course the work routine itself.

Perhaps it is a good idea to let the module of the work routine register the routine to the RPC layer. This makes it possible to implement a varying set of RPC's depending on which code is running in spind.

If we do that the description of parameters and result might need to be included. Currently we use a cJSON (nicknamed spin_data) struct both as input and as output.

##

Possible interface, something like:

	typedef result_t (*rpc_func)(param_t *params);
	rpc_bind(char *name, rpc_func *func, rpc_param *param, rpc_result *result);
	
with rpc_param a list of parameter descriptions(name, type), and rpc_result something similar for the result.
Rpc code can then parse the arguments and pass them as positional parameters in an array pointed to by . Similar for result.
