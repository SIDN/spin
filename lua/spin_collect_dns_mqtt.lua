#!/usr/bin/lua

local mqtt = require 'mosquitto'
local lnflog = require 'lnflog'
local bit = require 'bit'
local json = require 'json'
local dnscache = require 'dns_cache'
local arp = require 'arp'
local nc = require 'node_cache'
-- tmp: treat DNS traffic as actual traffic
local aggregator = require 'collect'
local filter = require 'filter'
filter:load(true)

local verbose = true
--
-- A simple DNS cache for answered queries
--

--
-- mqtt-related things
--
local DNS_COMMAND_CHANNEL = "SPIN/dnsnames"
local TRAFFIC_CHANNEL = "SPIN/traffic"

local node_cache = nc.NodeCache_create()
node_cache:set_arp_cache(arp)
node_cache:set_dns_cache(dnscache.dnscache)


function vprint(msg)
    if verbose then
        print("[SPIN/DNS] " .. msg)
    end
end

local client = mqtt.new()
client.ON_CONNECT = function()
    vprint("Connected to MQTT broker")
    client:subscribe(DNS_COMMAND_CHANNEL)
    vprint("Subscribed to " .. DNS_COMMAND_CHANNEL)
--        local qos = 1
--        local retain = true
--        local mid = client:publish("my/topic/", "my payload", qos, retain)
end

client.ON_MESSAGE = function(mid, topic, payload)
    vprint("got message on " .. topic)
    if topic == DNS_COMMAND_CHANNEL then
        command = json.decode(payload)
        if (command["command"] and command["argument"]) then
            vprint("Command: " .. payload)
            response = handle_command(command.command, command.argument)
            if response then
                local response_txt = json.encode(response)
                vprint("Response: " .. response_txt)
                client:publish(TRAFFIC_CHANNEL, response_txt)
            end
        end
    end
end

function handle_command(command, argument)
  local response = {}
  response["command"] = command
  response["argument"] = argument

  if (command == "ip2hostname") then
    local ip = argument
    --local names = get_dnames_from_cache(ip)
    local names = dnscache.dnscache:get_domains(ip)
    if #names > 0 then
      response["result"] = names
    else
      response = nil
    end
  elseif command == "missingNodeInfo" then
    -- just publish it again?
    local node = node_cache:get_by_id(tonumber(argument))
    print("[XX] WAS ASKED FOR INFO ABOUT NODE " .. argument .. "(type " .. type(argument) .. ")")

    publish_node_update(node)
    response = nil
  end
  return response
end

--
-- dns-related things
--


--vprint(lnflog.sin(lnflog.pi))

function print_array(arr)
    io.write("0:   ")
    for i,x in pairs(arr) do
      io.write(x)
      if (i == 0 or i % 10 == 0) then
        vprint("")
        io.write(i .. ": ")
        if (i < 100) then
          io.write(" ")
        end
      else
        io.write(" ")
      end
    end
    vprint("")
end

function get_dns_answer_info(event)
    -- check whether this is a dns answer event (source port 53)
    if event:get_octet(21) ~= 53 then
        return nil, "Event is not a DNS answer"
    end
    local dnsp = event:get_payload_dns()
    if not dnsp:is_response() or not (dnsp:get_qtype() == 1 or dnsp:get_qtype() == 28) then
        return nil, "DNS Packet is not an A/AAAA response packet"
    end
    --vprint(dnsp:tostring());
    --vprint("QUESTION NAME: " .. dnsp:get_qname())
    --vprint("QUESTION TYPE: " .. dnsp:get_qtype())
    --dname = dnsp:get_qname_second_level_only()
    dname = dnsp:get_qname()
    if dname == nil then
        return nil, err
    end
    addrs, err = dnsp:get_answer_address_strings()
    if addrs == nil then
        return nil, err
    end
    if table.getn(addrs) == 0 then
        return nil, "No addresses in DNS answer"
    end

    info = {}
    info.to_addr = event:get_to_addr()
    info.timestamp = event:get_timestamp()
    info.dname = dname
    info.ip_addrs = addrs
    return info
end

function my_cb(mydata, event)
    vprint("Event:")
    vprint("  from: " .. event:get_from_addr())
    vprint("  to:   " .. event:get_to_addr())
    vprint("  source port: " .. event:get_octet(21))
    vprint("  timestamp: " .. event:get_timestamp())
    vprint("  size: " .. event:get_payload_size())
    vprint("  hex:")
    --print_array(event:get_payload_hex());

    if event:get_octet(21) == 53 then
      vprint(get_dns_answer_info(event))
    end
end

local function publish_traffic(msg)
  --vprint("Yolo. callback: " .. msg)
  vprint("Publish traffic data: " .. msg)
  client:publish(TRAFFIC_CHANNEL, msg)
end

function print_dns_cb(mydata, event)
    if event:get_octet(21) == 53 then
      info, err = get_dns_answer_info(event)
      if info == nil then
        vprint("Notice: " .. err)
      else
        for _,addr in pairs(info.ip_addrs) do
          --add_to_cache(addr, info.dname, info.timestamp)
          update = dnscache.dnscache:add(addr, info.dname, info.timestamp)
          dnscache.dnscache:clean(info.timestamp - 10)
          if update then
            -- update the node cache, and publish if it changed
            local updated_node = node_cache:add_domain_to_ip(addr, info.dname)
            if updated_node then publish_node_update(updated_node) end
          end
          -- tmp: treat DNS queries as actual traffic
          --vprint("Adding flow")
          -- ADD TO DNS AGGREGATOR?...
          --add_flow(info.timestamp, info.to_addr, info.dname, 1, 1, publish_traffic, filter:get_filter_table())

        end
        --addrs_str = "[ " .. table.concat(info.ip_addrs, ", ") .. " ]"
        --vprint("Event: " .. info.timestamp .. " " .. info.to_addr .. " " .. info.dname .. " " .. addrs_str)
      end
    end
end

function print_blocked_cb(mydata, event)
  local from_ip = event:get_from_addr()
  local from = arp:get_hw_address(from_ip)
  if not from then
    from = from_ip
  end
  local to_ip = event:get_to_addr()
  local domains = dnscache.dnscache:get_domains(to_ip)
  if #domains > 0 then
    to_ip = domains[0]
  end

  print("[XX] PACKET GOT BLOCKED (TODO: report)")
  msg = { command = "blocked", argument = "", result = {
              timestamp = event:get_timestamp(),
              from = from,
              to = event:get_to_addr()
          }
        }
  client:publish(TRAFFIC_CHANNEL, json.encode(msg))
end

function publish_node_update(node)
  if node == nil then
    error("nil node")
  end
  local msg = {}
  msg.command = "nodeUpdate"
  msg.argument = ""
  msg.result = node
  local msg_json = json.encode(msg)
  print("[XX] publish to " .. TRAFFIC_CHANNEL .. ":")
  print(msg_json)
  client:publish(TRAFFIC_CHANNEL, msg_json)
end

function print_traffic_cb(mydata, event)
  --print("[XX] " .. event:get_timestamp() .. " " .. event:get_payload_size() .. " bytes " .. event:get_from_addr() .. " -> " .. event:get_to_addr())
  -- Find the node-id's of the IP addresses
  local from_node_id, to_node_id, new
  from_node, new = node_cache:add_ip(event:get_from_addr())
  if new then
    -- publish it to the traffic channel
    publish_node_update(from_node)
  end
  to_node, new = node_cache:add_ip(event:get_to_addr())
  if new then
    -- publish it to the traffic channel
    publish_node_update(to_node)
  end

  -- put internal nodes as the source
  if to_node.mac then
    from_node, to_node = to_node, from_node
  end

  add_flow(event:get_timestamp(), from_node.id, to_node.id, event:get_payload_size(), 1, publish_traffic, filter:get_filter_table())
end

vprint("SPIN experimental DNS capture tool")
broker = arg[1] -- defaults to "localhost" if arg not set
traffic = lnflog.setup_netlogger_loop(771, print_traffic_cb, mydata, 0.2)
dns = lnflog.setup_netlogger_loop(772, print_dns_cb, mydata, 0.2)
blocked = lnflog.setup_netlogger_loop(773, print_blocked_cb, nil, 0.2)
vprint("Connecting to broker")
client:connect(broker)
--nl:loop_forever()
vprint("Starting listen loop")
while true do
    dns:loop_once()
    traffic:loop_once()
    blocked:loop_once()
    client:loop()
end
nl:close()
