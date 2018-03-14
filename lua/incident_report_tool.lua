#!/usr/bin/lua

local mqtt = require 'mosquitto'
local json = require 'json'

local TRAFFIC_CHANNEL = "SPIN/traffic"
local INCIDENT_CHANNEL = "SPIN/incidents"

-- hey this could fetch it too!
-- do we have http request built in?
-- if so, fetch from known or configured address, check if correct json or translate csv,
-- then send to mqtt

-- right now, cli only for first attempt
-- also, not much input checking yet
-- src_addr and src_port are not actually used in history search by spin (might even be irrelevant)

function help(error)
    if error ~= nil then
        print("Error: " .. error)
        print("")
    end
    print("Usage: incident_report_tool.lua [options] timestamp dst_addr dst_port [src_addr] [src_port]")
    print("")
    print("Network-local tool to report malicious traffic incidents, so that SPIN can act upon it")
    print("")
    print("Options:")
    print("-m <host> mqtt host (defaults to 127.0.0.1)")
    print("-p <port> mqtt port (defaults to 1883)")
    os.exit()
end

-- returns:
-- mqtt_host, mqtt_port, timestamp, src_addr, src_port, dest_addr, dest_port
-- or none on error
function parse_args(args)
    local mqtt_host = "127.0.0.1"
    local mqtt_port = 1883
    local argcount = 0
    local src_addr = "1.2.3.4"
    local src_port = "12345"
    skip = false
    for i = 1,table.getn(args) do
        if skip then
            skip = false
        elseif arg[i] == "-h" then
            help()
        elseif arg[i] == "-m" then
            mqtt_host = arg[i+1]
            if mqtt_host == nil then help("missing argument for -m") end
            skip = true
        elseif arg[i] == "-p" then
            mqtt_port = arg[i+1]
            if mqtt_host == nil then help("missing argument for -p") end
            skip = true
        elseif argcount == 0 then
            timestamp = tonumber(arg[i])
            if timestamp == nil then help("Timestamp must be an integer: " .. arg[i]) end
            argcount = argcount + 1
        elseif argcount == 1 then
            dst_addr = arg[i]
            argcount = argcount + 1
        elseif argcount == 2 then
            dst_port = tonumber(arg[i])
            if dst_port == nil then help("dst_port must be an integer") end
            argcount = argcount + 1
        elseif argcount == 3 then
            src_addr = arg[i]
            argcount = argcount + 1
        elseif argcount == 4 then
            src_port = tonumber(arg[i])
            if src_port == nil then help("src_port must be an integer") end
            argcount = argcount + 1
        else
            help("Too many arguments at " .. table.getn(args))
        end
    end
    if argcount < 3 then
        help("Missing arguments")
    end
    return mqtt_host, mqtt_port, timestamp, src_addr, src_port, dst_addr, dst_port
end


local mqtt_host, mqtt_port, timestamp, src_addr, src_port, dst_addr, dst_port = parse_args(arg)

local incident_msg_table = {}
incident_msg_table["timestamp"] = timestamp
incident_msg_table["src_addr"] = src_addr
incident_msg_table["src_port"] = src_port
incident_msg_table["dst_addr"] = src_addr
incident_msg_table["dst_port"] = src_port
-- this messes up the order, is that a problem? do we want to hand-build?
local incident_msg_json =
'{ "incident": { ' ..
    '"timestamp": ' .. timestamp .. ',' ..
    '"src_addr": "' .. src_addr .. '",' ..
    '"src_port": ' .. src_port .. ',' ..
    '"dst_addr": "' .. dst_addr .. '",' ..
    '"dst_port": ' .. dst_port .. ',' ..
    '"severity": ' .. 5 .. ',' ..
    '"type": ' .. '"ddos"' .. ',' ..
    '"name": ' .. '"malware.evil.prototype"' ..
'} }'



local client = mqtt.new()

client.ON_CONNECT = function()
    client:publish(INCIDENT_CHANNEL, incident_msg_json)
end

client.ON_PUBLISH = function()
    client:disconnect()
end

client:connect(mqtt_host, mqtt_port)

client:loop_forever()
