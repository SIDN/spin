
-- RPC abstraction
--
-- If available, this module uses UBUS for RPC calls, with the namespace
-- 'spin' and the procedure 'rpc'; the arguments themselves contain
-- the final method and optional parameters, for which the caller
-- is responsible
--
-- If ubus is not available, this module uses JSON-RPC 2.0


local socket = require("socket")
local socket_unix = require("socket.unix")
local have_ubus, ubus = pcall(require, 'ubus')
local json = require 'json'

-- connect to a rpc server, returns an connection object with
-- a 'call' method
local json_rpc_connect = function (opts)
    print("[XX] connect")
    local s = assert( socket.unix() )
    -- TODO: configuration, error handling
    assert( s:connect("/tmp/spin.sock") )
    local conn = {}
    conn.s = s
    -- TODO: we have this compatibility the wrong way around! we need to add these arguments if
    -- it is ubus, rather than ignore them if it is not
    conn.call = function(self, command)
        print("[XX] json-rpc called.")
        -- add or override json-rpc version
        command['jsonrpc'] = "2.0"
        command['id'] = math.random(0,1000000)
        -- convert to string
        local command_str = json.encode(command)
        result, err = s:send(command_str)
        print("[XX] command sent")
        if result then
            print("[XX] receiving response")
            response, err = s:receive("*a")
            print("[XX] GOT RESPONSE: " .. response)
            response_json = json.decode(response)
            return response_json
        end
        return None, "error"
    end
    return conn
end

local ubus_rpc_connect = function (opts)
    local conn = {}
    conn.ubus_conn = ubus.connect()
    if not conn.ubus_conn then
        return None, "Failed to connect to UBUS"
    end
    conn.call = function(self, command)
        print("[XX] ubus call()")
        return self.ubus_conn:call("spin", "rpc", command)
    end
    return conn
end

local _M = {}
if have_ubus then
    _M.connect = ubus_rpc_connect
else
    _M.connect = json_rpc_connect
end
return _M
