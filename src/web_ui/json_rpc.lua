
-- RPC abstraction
--
-- This module handles the RPC part of the WEB API
--
-- Externally, this is representated as an Web API endpoint that talks
-- JSON-RPC
--
-- Internally, if available, this module uses UBUS for RPC calls,
-- with the namespace 'spin' and the procedure 'rpc';
-- the arguments themselves contain the final method and optional
-- parameters, for which the caller is responsible.
--
-- If ubus is not available, this module uses JSON-RPC 2.0 internally
-- as well (i.e. the actual RPC method call is passed through as-is,
-- and so is the response), through a local unix domain socket.
--


local socket = require("socket")
local socket_unix = require("socket.unix")
local have_ubus, ubus = pcall(require, 'ubus')
local json = require 'json'

-- connect to a rpc server, returns an connection object with
-- a 'call' method
local json_rpc_connect = function (opts)
    local s = assert( socket.unix() )
    -- TODO: configuration, error handling
    local result, err = s:connect("/var/run/spin_rpc.sock")
    if not result then
        return nil, "Cannot connect to JSON-RPC domain socket, is spind running? error: " .. err
    end
    local conn = {}
    conn.s = s
    -- TODO: we have this compatibility the wrong way around! we need to add these arguments if
    -- it is ubus, rather than ignore them if it is not
    conn.call = function(self, command)
        -- add or override json-rpc version
        command['jsonrpc'] = "2.0"
        command['id'] = math.random(0,1000000)
        -- convert to string
        local command_str = json.encode(command)
        result, err = s:send(command_str)
        if result then
            response, err = s:receive("*a")
            response_json = json.decode(response)
            return response_json
        end
        return nil, "No response from JSON-RPC socket"
    end
    conn.close = function()
        s:close()
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
        if command['jsonrpc'] ~= '2.0' then
            local error_response = {}
            error_response['error'] = 'not JSON-RPC 2.0'
            return error_response
        end
        if command['id'] == nil then
            local error_response = {}
            error_response['error'] = "missing 'id' in JSON-RPC 2.0 request"
            return error_response
        end
        command['id'] = nil
        command['jsonrpc'] = nil
        print(json.encode(command))
        local ubus_response, err, e2, e3 = self.ubus_conn:call("spin", "rpc", command)
        if ubus_response == nil then
            ubus_response = {}
            ubus_response['error'] = { message='Error calling ubus spin. SPIN function may have not been registered.', code=err }
        end
        -- transform it back to a correct JSON-RPC 2.0 response
        ubus_response['jsonrpc'] = '2.0'
        ubus_response['id'] = command['id']
        return ubus_response
    end
    conn.close = function()
        self.ubus_conn:close()
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
