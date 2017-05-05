#!/usr/bin/lua

local mqtt = require 'mosquitto'
local lnflog = require 'lua-spin_nflog'
local bit = require 'bit'
local json = require 'json'
local dnscache = require 'dns_cache'
local arp = require 'arp'
local nc = require 'node_cache'
-- tmp: treat DNS traffic as actual traffic
local aggregator = require 'collect'
local firewall = require 'spin_firewall'
local filter = require 'filter'
filter:load(true)

local verbose = true
--
-- A simple DNS cache for answered queries
--

--
-- mqtt-related things
--
local COMMAND_CHANNEL = "SPIN/commands"
local TRAFFIC_CHANNEL = "SPIN/traffic"

local node_cache = nc.NodeCache_create()
node_cache:set_arp_cache(arp)
node_cache:set_dns_cache(dnscache.dnscache)
node_cache:set_filter_tool(filter)

function vprint(msg)
    if verbose then
        print("[SPIN/DNS] " .. msg)
    end
end

function vwrite(msg)
    if verbose then
        io.write(msg)
    end
end

local client = mqtt.new()
client.ON_CONNECT = function()
    vprint("Connected to MQTT broker")
    client:subscribe(COMMAND_CHANNEL)
    vprint("Subscribed to " .. COMMAND_CHANNEL)

    -- Reset any running clients
    client:publish(TRAFFIC_CHANNEL, json.encode(create_server_restart_command()))
--        local qos = 1
--        local retain = true
--        local mid = client:publish("my/topic/", "my payload", qos, retain)
end

client.ON_MESSAGE = function(mid, topic, payload)
    vprint("got message on " .. topic)
    vprint("from: " .. mid)
    if topic == COMMAND_CHANNEL then
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

function create_server_restart_command()
  local update  = {}
  update ["command"] = "serverRestart"
  update ["argument"] = ""
  return update
end

function create_filter_list_command()
  local update  = {}
  update ["command"] = "filters"
  update ["argument"] = ""
  update ["result"] = filter:get_filter_list()
  return update
end

function create_name_list_command()
  local update  = {}
  update ["command"] = "names"
  update ["argument"] = ""
  update ["result"] = filter:get_name_list()
  return update
end

function add_filters_for_node(node_id)
  local node = node_cache:get_by_id(node_id)
  if not node then return end
  filter:load()
  for _,ip in pairs(node.ips) do
    filter:add_filter(ip)
  end
  filter:save()
end

function remove_filters_for_node(node_id)
  local node = node_cache:get_by_id(node_id)
  if not node then return end
  filter:load()
  for _,ip in pairs(node.ips) do
    filter:remove_filter(ip)
  end
  filter:save()
end

function add_name_for_node(node_id, name)

  local node = node_cache:get_by_id(node_id)
  if not node then return end
  filter:load()
  for _,ip in pairs(node.ips) do
    filter:add_name(ip, name)
  end
  filter:save()
end

function is_ipv6_ip(ip)
    return ip:find(":")
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
  elseif (command == "get_filters") then
    response = create_filter_list_command()
  elseif (command == "add_filter") then
    -- response necessary? resend the filter list?
    add_filters_for_node(argument)
    -- don't send direct response, but send a 'new list' update
    response = create_filter_list_command()
  elseif (command == "remove_filter") then
    filter:load()
    filter:remove_filter(argument)
    filter:save()
    -- don't send direct response, but send a 'new list' update
    response = create_filter_list_command()
  elseif (command == "reset_filters") then
    filter:remove_all_filters()
    filter:add_own_ips()
    filter:save()
    response = create_filter_list_command()
  elseif (command == "get_names") then
    response = create_name_list_command()
  elseif (command == "add_name") then
    add_name_for_node(argument["node_id"], argument["name"])
    -- rebroadcast names?
    response = nil
  elseif command == "missingNodeInfo" then
    -- just publish it again?
    local node = node_cache:get_by_id(tonumber(argument))
    publish_node_update(node)
    response = nil
  elseif command == "blockdata" then
    -- add block to iptables
    response = nil
    local fw = firewall.SpinFW_create(false)
    local fw6 = firewall.SpinFW_create(true)
    fw:read()
    fw6:read()
    local node = node_cache:get_by_id(argument)
    if node then
      for _,ip in pairs(node.ips) do
        print("[XX] ADDING BLOCK FOR IP: " .. ip)
        if is_ipv6_ip(ip) then
          print("[XX] IS IPV6")
          fw6:add_block_ip(ip)
        else
          print("[XX] IS ipv4")
          fw:add_block_ip(ip)
        end
      end
      fw:commit()
      fw6:commit()
    end
  elseif command == "stopblockdata" then
    -- remove block from iptables
    response = nil
    local fw = firewall.SpinFW_create(false)
    local fw6 = firewall.SpinFW_create(true)
    fw:read()
    fw6:read()
    local node = node_cache:get_by_id(argument)
    if node then
      for _,ip in pairs(node.ips) do
        if is_ipv6_ip(ip) then
          fw6:remove_block_ip(ip)
        else
          fw:remove_block_ip(ip)
        end
      end
      fw:commit()
      fw6:commit()
    end
  elseif command == "allowdata" then
    -- add allow to iptables
  elseif command == "stopallowdata" then
    -- stop allow from iptables
  elseif command == "debugNodeById" then
    response = nil
    client:publish("SPIN/debug", json.encode(node_cache:get_by_id(tonumber(argument))))
  elseif command == "debugNodesByDNS" then
    response = nil
    client:publish("SPIN/debug", json.encode(node_cache:get_by_domain(argument)))
  else
    response = {}
    response["command"] = "error"
    response["argument"] = "Unknown command: " .. command
  end
  return response
end

--
-- dns-related things
--


--vprint(lnflog.sin(lnflog.pi))

function print_array(arr)
    vwrite("0:   ")
    for i,x in pairs(arr) do
      vwrite(x)
      if (i == 0 or i % 10 == 0) then
        vwrite("\n")
        vwrite(i .. ": ")
        if (i < 100) then
          vwrite(" ")
        end
      else
        vwrite(" ")
      end
    end
    vwrite("\n")
end

function get_dns_answer_info(event)
    -- check whether this is a dns answer event (source port 53)
    if event:get_src_port() ~= 53 then
        return nil, "Event is not a DNS answer"
    end
    local dnsp = event:get_payload_dns()
    if not dnsp then
        vwrite("DNS packet malgormed\n")
        vwrite("Packet hex data:\n")
        print_array(event:get_payload_hex())
        return nil, "DNS Packet is malformed or not an A/AAAA response packet"
    end
    if not dnsp:is_response() or not (dnsp:get_qtype() == 1 or dnsp:get_qtype() == 28) then
        return nil, "DNS Packet is not an A/AAAA response packet"
    end
    --vwrite("Packet hex data:\n")
    --print_array(event:get_payload_hex())
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
    info.to_addr = event:get_dst_addr()
    info.timestamp = event:get_timestamp()
    info.dname = dname
    info.ip_addrs = addrs
    return info
end

function my_cb(mydata, event)
    vprint("Event:")
    vprint("  from: " .. event:get_src_addr())
    vprint("  to:   " .. event:get_dst_addr())
    vprint("  source port: " .. event:get_octet(21))
    vprint("  timestamp: " .. event:get_timestamp())
    vprint("  size: " .. event:get_payload_size())
    vprint("  hex:")
    --print_array(event:get_payload_hex());

    if event:get_src_port() == 53 then
      vprint(get_dns_answer_info(event))
    end
end

local function publish_traffic(msg)
  print("[XX] Publish traffic data: " .. msg)
  local o = json.decode(msg)
  for _,f in pairs(o.result.flows) do
    f.from = node_cache:get_by_id(f.from)
    f.to = node_cache:get_by_id(f.to)
  end
  print("[XX] NEW TRAF MSG")
  print(json.encode(o))
  print("[XX] END NEW TRAF MSG")
  --client:publish(TRAFFIC_CHANNEL, msg)
  client:publish(TRAFFIC_CHANNEL, json.encode(o))
end

function print_dns_cb(mydata, event)
    if event:get_src_port() == 53 then
      info, err = get_dns_answer_info(event)
      if info == nil then
        --vprint("Notice: " .. err)
      else
        for _,addr in pairs(info.ip_addrs) do
          --add_to_cache(addr, info.dname, info.timestamp)
          update = dnscache.dnscache:add(addr, info.dname, info.timestamp)
          --dnscache.dnscache:clean(info.timestamp - 10)
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
  from_node, new = node_cache:add_ip(event:get_src_addr())
  if new then
    -- publish it to the traffic channel
    publish_node_update(from_node)
  end

  to_node, new = node_cache:add_ip(event:get_dst_addr())
  if new then
    -- publish it to the traffic channel
    publish_node_update(to_node)
  end

  -- put internal nodes as the source
  if to_node.mac then
    from_node, to_node = to_node, from_node
  end

  msg = { command = "blocked", argument = "", result = {
              timestamp = event:get_timestamp(),
              from = from_node.id,
              to = to_node.id
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
  --print("[XX] publish to " .. TRAFFIC_CHANNEL .. ":")
  --print(msg_json)
  client:publish(TRAFFIC_CHANNEL, msg_json)
end

function print_traffic_cb(mydata, event)
  --print("[XX] " .. event:get_timestamp() .. " " .. event:get_payload_size() .. " bytes " .. event:get_src_addr() .. " -> " .. event:get_dst_addr())
  -- Find the node-id's of the IP addresses
  print(event:get_src_addr() .. " -> " .. event:get_dst_addr())
  local from_node_id, to_node_id, new
  from_node, new = node_cache:add_ip(event:get_src_addr())
  if new then
    -- publish it to the traffic channel
    publish_node_update(from_node)
  end

  to_node, new = node_cache:add_ip(event:get_dst_addr())
  if new then
    -- publish it to the traffic channel
    publish_node_update(to_node)
  end

  -- put internal nodes as the source
  if to_node.mac then
    from_node, to_node = to_node, from_node
  end

  add_flow(event:get_timestamp(), from_node.id, to_node.id, 1, event:get_payload_size(), publish_traffic, filter:get_filter_table())
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
    -- clean data older than 15 minutes
    local clean_before = os.time() - 900
    node_cache:clean(clean_before)
    dnscache.dnscache:clean(clean_before)
end
nl:close()
