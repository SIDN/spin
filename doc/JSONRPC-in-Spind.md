# JSON-RPC in spind

Spind has an interface for JSON-RPC calls.
JSON-RPC is described in https://en.wikipedia.org/wiki/JSON-RPC and we implement version 2.0.

The interface in spind is a routine

	char *call_string_jsonrpc(char *args);

which gets a string, decodes it as a JSON-RPC, executes it, and returns a valid JSON-RPC reply string, or NULL if the JSON-RPC call was a notification.

For testing purposes we currently get the call as a message on the Mosquitto topic SPIN/jsonrpc/q and we send the reply(if any) on SPIN/jsonrpc/a

The JSON-RPC calls are expected to come from a lua server running on the same machine, so we could change this to communication on a UNIX-socket.

There is already code to translate UBUS calls to JSON-RPC, but this code can maybe better move to the lua server.

