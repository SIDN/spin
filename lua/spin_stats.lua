#!/usr/bin/lua

local mqtt = require 'mosquitto'
local json = require 'json'

local TRAFFIC_CHANNEL = "SPIN/traffic"

local HISTORY_SIZE = 300
local PRINT_INTERVAL = 60

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

function history_stats()
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

    --print("[XX] " .. json.encode(nodes))
    for frm,a in pairs(nodes) do
      print("[XX] DEVICE: " .. frm .. "(" .. squash_node_info(node_info[frm]) ..")")
      local size_count = 0
      local port_count = 0
      local port_most_used = nil
      local to_count = 0
      for to,b in pairs(a) do
        --print("    " .. squash_node_info(node_info[to]))
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
          --print("[XX] " .. squash_node_info(node_info[frm]) .. " -> " .. squash_node_info(node_info[to]) .. " " .. port .. " " .. size)
          end
      end
      print("    contacted " .. to_count .. " external nodes")
      print("    over at total of " .. port_count .. " addr/port combinations")
      print("    for a total (outgoing) size of " .. size_count)
      print("    used " .. table_count(ports) .. " different ports")
      print("    port most used: " .. port_most_used)
      for p,c in pairs(ports) do
          print("      " .. p .. "  (" .. c .. " times)")
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
    --print(json.encode(payload))
    local timestamp = payload["timestamp"]
    history[timestamp] = payload["flows"]
    history_clean()
end

client.ON_MESSAGE = function(mid, topic, payload)
    local pd = json.decode(payload)
    if pd["command"] and pd["command"] == "traffic" then
        handle_traffic_message(pd["result"])
    end
end


vprint("SPIN stats tool")
broker = arg[1] -- defaults to "localhost" if arg not set
client:connect(broker)
vprint("connected")

local last_print = os.time()
while true do
    local cur = os.time()
    if (cur > last_print + PRINT_INTERVAL) then
        --history_print()
        history_stats_full()
        history_stats()
        print("")
        last_print = cur
    end

    client:loop()
end
