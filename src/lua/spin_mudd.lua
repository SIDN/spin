#!/usr/bin/lua

local mqtt = require 'mosquitto'
local json = require 'json'
local luamud = require 'luamud'

local TRAFFIC_CHANNEL = "SPIN/traffic"

local HISTORY_SIZE = 300
local PRINT_INTERVAL = 10

local verbose = true

function vprint(msg)
    if verbose then
        print("[SPIN/mqtt] " .. msg)
    end
end

local client = mqtt.new()

client.ON_CONNECT = function()
    vprint("Connected to MQTT broker")
    client:subscribe(TRAFFIC_CHANNEL)
    vprint("Subscribed to " .. TRAFFIC_CHANNEL)
end

function block_node(node)
    for _,v in pairs(node["ips"]) do
        local cmd = "spin_config block add " .. v
        print(cmd)
        os.execute(cmd)
    end
end

local mud = luamud.mud_create_from_file("tests/mud_dns_only.json")

function handle_traffic_message(payload)
    --local timestamp = payload["timestamp"]
    --history[timestamp] = payload["flows"]
    --history_clean()
    -- for each flow, we loop through all:
    -- - from ip addresses
    -- - from domain names
    -- - to ip addresses
    -- - to domain names
    -- Depending on which mac address is set (from or to or none)
    -- this should check from_device or to_device policies
    for _,flow in pairs(payload["flows"]) do
        local from_device
        local ips
        local domains
        local port
        if flow["from"]["mac"] ~= nil then
            from_device = true
            ips = flow["to"]["ips"]
            domains = flow["to"]["domains"]
            to_port = flow["to_port"]
            from_port = flow["from_port"]
        elseif flow["to"]["mac"] ~= nil then
            to_device = true
            ips = flow["from"]["ips"]
            domains = flow["from"]["domains"]
            to_port = flow["to_port"]
            from_port = flow["from_port"]
        else
            print("[XX] ignoring flow, not from or to device (no mac known)")
            break;
        end
        local actions = mud:get_policy_actions(from_device, ips, domains, from_port, to_port)
        print(actions)
    end
end

client.ON_MESSAGE = function(mid, topic, payload)
    local pd = json.decode(payload)
    if pd["command"] and pd["command"] == "traffic" then
        handle_traffic_message(pd["result"])
    end
end


vprint("SPIN MUD test tool")
broker = arg[1] -- defaults to "localhost" if arg not set
client:connect(broker)
vprint("connected")

local last_print = os.time()
while true do
    local cur = os.time()
    client:loop()
end
