#!/usr/bin/lua

local mqtt = require 'mosquitto'
local json = require 'json'

local TRAFFIC_CHANNEL = "SPIN/traffic"
local INCIDENT_CHANNEL = "SPIN/incidents"

local HISTORY_SIZE = 600

local verbose = true

function vprint(msg)
    if verbose then
        print("[SPIN/mqtt] " .. msg)
    end
end

local history = {}

function history_size()
    local result = 0
    for _,_ in pairs(history) do
        result = result + 1
    end
    return result
end

function squash_node_info(node_info)
    local result = ""
    if node_info["name"] then result = node_info["name"] .. "/" end
    for _,v in pairs(node_info["domains"]) do
        result = result .. v .. "/"
    end
    for _,v in pairs(node_info["ips"]) do
        result = result .. v .. "/"
    end
    result = result:sub(1,-2)
    return result
end

function table_count(t)
    local result = 0
    for _,_ in pairs(t) do
        result = result + 1
    end
    return result
end

function simple_cmp(a, b)
    return a > b
end

function get_keys_sorted_by_value(tbl, sort_function)
    local keys = {}
    for key in pairs(tbl) do
        table.insert(keys, key)
    end

    table.sort(keys, function(a, b)
        return sort_function(tbl[a], tbl[b])
    end)

    return keys
end

function block_ip(ip)
	local cmd = "spin_config block add " .. ip
	print(cmd)
	os.execute(cmd)
end

function block_node(node)
    for _,v in pairs(node["ips"]) do
        local cmd = "spin_config block add " .. v
        print(cmd)
        os.execute(cmd)
    end
end

function history_stats(limit)
    local nodes = {}
    local node_info = {}

    for _,v in pairs(history) do
        for _,f in pairs(v) do
            if f["from"]["mac"] ~= nil then
                local from_node_id = f["from"]["id"]
                node_info[from_node_id] = f["from"]
                local to_node_id = f["to"]["id"]
                node_info[to_node_id] = f["to"]
                local port = f["to_port"]
                local size = f["size"]
                if nodes[from_node_id] == nil then
                    nodes[from_node_id] = {}
                end
                if nodes[from_node_id][to_node_id] == nil then
                    nodes[from_node_id][to_node_id] = {}
                end
                if nodes[from_node_id][to_node_id][port] == nil then
                    nodes[from_node_id][to_node_id][port] = size
                else
                    nodes[from_node_id][to_node_id][port] = nodes[from_node_id][to_node_id][port] + size
                end
            end
        end
    end

    local ports = {}
    local dests = {}

    for frm,a in pairs(nodes) do
        local size_count = 0
        local port_count = 0
        local port_most_used = nil
        local to_count = 0
        for to,b in pairs(a) do
            to_count = to_count + 1
            for port,size in pairs(b) do
                if ports[port] == nil then
                    ports[port] = 1
                else
                    ports[port] = ports[port] + 1
                end
                port_count = port_count + 1
                size_count = size_count + size
                if port_most_used == nil then
                    port_most_used = port
                else
                    if ports[port] > ports[port_most_used] then
                        port_most_used = port
                    end
                end
            end
        end
        local ni = node_info[frm]
        local devicename = "<unknown>"
        if ni["name"] ~= nil then
            devicename = ni["name"]
        elseif ni["mac"] ~= nil then
            devicename = ni["mac"]
        elseif table.getn(ni["domains"]) > 0 then
            devicename = ni["domains"][0]
        elseif table.getn(ni["ips"]) > 0 then
            devicename = ni["ips"][0]
        end
        print("Device: " .. devicename)
        print("    contacted " .. to_count .. " external nodes")
        print("    over at total of " .. port_count .. " addr/port combinations")
        print("    for a total (outgoing) size of " .. size_count)
        print("    used " .. table_count(ports) .. " different ports")
        print("    top 10 most used ports: ")
        local sorted_ports = get_keys_sorted_by_value(ports, simple_cmp)
        local i = 0
        for _,k in pairs(sorted_ports) do
            print("      " .. k .. "  (" .. ports[k] .. " times)")
            i = i + 1
            if i >= 10 then break end
        end
        if limit > 0 and port_count > limit then
            block_node(node_info[frm])
        end

    end

    return nodes
end

function history_stats_full()
    local nodes = {}
    local node_info = {}
    for _,v in pairs(history) do
        for _,f in pairs(v) do
            local from_node_id = f["from"]["id"]
            node_info[from_node_id] = f["from"]
            local to_node_id = f["to"]["id"]
            node_info[to_node_id] = f["to"]
            local p1 = f["from_port"]
            local p2 = f["to_port"]
            local port
            if p1 > p2 then
                port = p1 .. "->" .. p2
            else
                port = p1 .. "->" .. p2
            end
            --local src_port = f["from_port"]
            --local dest_port = src_port .. "-" .. f["to_port"]
            local size = f["size"]
            if nodes[from_node_id] == nil then
                nodes[from_node_id] = {}
            end
            if nodes[from_node_id][to_node_id] == nil then
                nodes[from_node_id][to_node_id] = {}
            end
            --if nodes[from_node_id][to_node_id][src_port] ~= nil then
            --    nodes[from_node_id][to_node_id][src_port] = nodes[from_node_id][to_node_id][src_port] + size
            --elseif nodes[from_node_id][to_node_id][dest_port] == nil then
            if nodes[from_node_id][to_node_id][port] == nil then
                nodes[from_node_id][to_node_id][port] = size
            else
                nodes[from_node_id][to_node_id][port] = nodes[from_node_id][to_node_id][port] + size
            end
        end
    end

    --print("[XX] " .. json.encode(nodes))
    for frm,a in pairs(nodes) do
      for to,b in pairs(a) do
        for port,size in pairs(b) do
          print("[XX] " .. squash_node_info(node_info[frm]) .. " -> " .. squash_node_info(node_info[to]) .. " " .. port .. " " .. size)
        end
      end
    end


    return nodes
end

function history_print()
    for i,v in pairs(history) do
        print(i .. ":")
        for _,f in pairs(v) do

            print(" " .. json.encode(f))
        end
    end
end

-- returns the new size of the history
function history_clean()
    local timestamp = os.time() - HISTORY_SIZE
    local newsize = 0
    for i,_ in pairs(history) do
        if i < timestamp then
            history[i] = nil
        else
            newsize = newsize + 1
        end
    end
    return newsize
end

function handle_traffic_message(payload)
    local timestamp = payload["timestamp"]
    history[timestamp] = payload["flows"]
    history_clean()
end

function handle_incident_report(incident, timestamp)
    print("[XX] Incident report receiver, timestamp: " .. timestamp)
    print("[XX] incident reported:")
    print(json.encode(incident))
    --local timestamp = incident["incident_timestamp"]
    local dst_addr = incident["dst_addr"]
    local dst_port = incident["dst_port"]
    local hist_entry = history[timestamp]
    if hist_entry == nil then
        print("[XX] Incident not found in history")
        --print("Incident not found in history; have timestamps for: ")
        --for t,_ in pairs(history) do
        --    print(t)
        --end
    else
        -- we are looking for dst_addr:dst_port
        -- it can both be the from_port
        for _,flow in pairs(hist_entry) do
            for _,ip in pairs(flow["to"]["ips"]) do
                --print("[XX] to: " .. ip .. ":" .. flow["to_port"])
                if ip == dst_addr and flow["to_port"] == dst_port then
                    print("[XX] Incident found in history! It was:")
                    print(json.encode(flow["from"]))
                    if flow["from"]["mac"] ~= nil then
                        print("[XX] Blocking all traffic from and to addresses: ")
                        for _,v in pairs(flow["from"]["ips"]) do
                            print("[XX] " ..v)
                            block_ip(v)
                        end
                        return true
                    else
                        print("Reported incident does not appear to concern a local device")
                    end
                    return false
                end
            end
            for _,ip in pairs(flow["from"]["ips"]) do
                --print("[XX] from ip: " .. ip .. ":" .. flow["from_port"])
                if ip == dst_addr and flow["from_port"] == dst_port then
                    print("[XX] Incident found in history! It was:")
                    print(json.encode(flow["to"]))
                    if flow["to"]["mac"] ~= nil then
                        print("[XX] Blocking all traffic from and to addresses: ")
                        for _,v in pairs(flow["to"]["ips"]) do
                            print("[XX] " .. v)
                            block_ip(v)
                        end
                        return true
                    else
                        print("Reported incident does not appear to concern a local device")
                    end
                    return false
                end
            end
        end
        print("[XX] incident not found.")
    end
    return false
end

function help(error)
    if error ~= nil then
        print("Error: " .. error)
        print("")
    end
    print("Usage: spin_enforcer.lua [options]")
    print("")
    print("Tracks traffic patterns and applies prototype policy enforcement by blocking devices")
    print("")
    print("Options:")
    print("-h               show this help")
    print("-m <host>        mqtt host (defaults to 127.0.0.1)")
    print("-n <port>        mqtt port (defaults to 1883)")
    print("-l <limit>       Block a devices if contacts more than <limit> address/port combinations (defaults to disabled)")
    print("-i               Listens to provider API incident reports (on the MQTT topic SPIN/incidents, defaults to disabled)")
    print("-s <interval>    Show traffic statistics every <interval> seconds (defaults to disabled)")
    print("-k <seconds>     Keep history for <seconds> seconds (defaults to 600 seconds)")
    os.exit()
end

function parse_args(args)
    local mqtt_host = "127.0.0.1"
    local mqtt_port = 1883
    local limit = 0
    local handle_incidents = false
    local print_interval = 0
    local keep_history_time = 600

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
        elseif arg[i] == "-n" then
            mqtt_port = tonumber(arg[i+1])
            if mqtt_port == nil then help("missing or bad argument for -n") end
            skip = true
        elseif arg[i] == "-l" then
            listen_host = arg[i+1]
            if listen_host == nil then help("missing argument for -l") end
            skip = true
        elseif arg[i] == "-i" then
            handle_incidents = true
        elseif args[i] == "-s" then
            print_interval = tonumber(arg[i+1])
            if print_interval == nil then help("missing argument for -i") end
            if print_interval <= 0 then help("print interval must be > 0") end
            skip = true
        elseif args[i] == "-k" then
            keep_history_time = tonumber(arg[i+1])
            if keep_history_time == nil then help("missing or bad argument for -k") end
            skip = true
        else
            help("Too many arguments at " .. table.getn(args))
        end
    end
    return mqtt_host, mqtt_port, limit, handle_incidents, print_interval, keep_history_time
end


mqtt_host, mqtt_port, limit, handle_incidents, print_interval, keep_history_time = parse_args(arg)

HISTORY_SIZE = keep_history_time

local client = mqtt.new()

client.ON_CONNECT = function()
    vprint("Connected to MQTT broker")
    client:subscribe(TRAFFIC_CHANNEL)
    vprint("Subscribed to " .. TRAFFIC_CHANNEL)
    if handle_incidents then
        client:subscribe(INCIDENT_CHANNEL)
        vprint("Subscribed to " .. INCIDENT_CHANNEL)
    end
end

client.ON_MESSAGE = function(mid, topic, payload)
    local pd = json.decode(payload)
    if topic == TRAFFIC_CHANNEL then
        if pd["command"] and pd["command"] == "traffic" then
            handle_traffic_message(pd["result"])
        end
    elseif handle_incidents and topic == INCIDENT_CHANNEL then
        if pd["incident"] == nil then
            print("Error: no incident data found in " .. payload)
            print("Incident report ignored")
        else
            local incident = pd["incident"]
            local ts = incident["incident_timestamp"]
            for i=ts-5,ts+5 do
                if handle_incident_report(incident, i) then break end
            end
        end
    end
end

vprint("SPIN policy enforcer, connecting to " .. mqtt_host .. ":" .. mqtt_port)
client:connect(mqtt_host, mqtt_port)
vprint("connected")


local last_print = os.time()
while true do
    local cur = os.time()
    if (print_interval > 0 and cur > last_print + print_interval) then
        history_stats(limit)
        last_print = cur
    end

    client:loop()
end
