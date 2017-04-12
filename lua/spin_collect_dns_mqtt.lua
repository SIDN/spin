#!/usr/bin/lua

local mqtt = require 'mosquitto'
local lnflog = require 'lnflog'
local bit = require 'bit'
local json = require 'json'

local verbose = false

--
-- mqtt-related things
--
local DNS_COMMAND_CHANNEL = "SPIN/dnsnames"
local TRAFFIC_CHANNEL = "SPIN/traffic"

function vprint(msg)
    if verbose then
        vprint(msg)
    end
end

local client = mqtt.new()
client.ON_CONNECT = function()
    vprint("[XX] Connected to MQTT broker")
    client:subscribe(DNS_COMMAND_CHANNEL)
    vprint("[XX] Subscribed to " .. DNS_COMMAND_CHANNEL)
--        local qos = 1
--        local retain = true
--        local mid = client:publish("my/topic/", "my payload", qos, retain)
end

client.ON_MESSAGE = function(mid, topic, payload)
    vprint("[XX] got message on " .. topic)
    if topic == DNS_COMMAND_CHANNEL then
        command = json.decode(payload)
        if (command["command"] and command["argument"]) then
            vprint("[XX] Command: " .. payload)
            response = handle_command(command.command, command.argument)
            if response then
                local response_txt = json.encode(response)
                vprint("[XX] Response: " .. response_txt)
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
    local names = get_dnames_from_cache(ip)
    if #names > 0 then
      response["result"] = names
    else
      response = nil
    end
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

function get_dns_qname(event)
    local result = ""
    local cur_di = 40
    local labellen = event:get_octet(cur_di)

    cur_di = cur_di + 1
    while labellen > 0 do
      label_ar, err = event:get_octets(cur_di, labellen)
      if label_ar == nil then
        vprint(err)
        return err, cur_di
      end
      for _,c in pairs(label_ar) do
        result = result .. string.char(c)
      end
      result = result .. "."
      cur_di = cur_di + labellen
      labellen = event:get_octet(cur_di)
      cur_di = cur_di + 1
    end
    return result, cur_di
end

function dns_skip_dname(event, i)
    --vprint("[XX] Get octet at " .. i)
    local labellen = event:get_octet(i)
    if bit.band(labellen, 0xc0) then
        --vprint("[XX] is dname shortcut")
        return i + 2
    end
    while labellen > 0 do
      i = i + 1 + labellen
--      vprint("Get octet at " .. i)
      labellen = event:get_octet(i)
      if bit.band(labellen, 0xc0) then
          --vprint("[XX] is dname shortcut")
          return i + 2
      end
    end
    return i
end

function get_dns_answer_info(event)
    -- check whether this is a dns answer event (source port 53)
    if event:get_octet(21) ~= 53 then
        return nil, "Event is not a DNS answer"
    end
    local dnsp = event:get_payload_dns()
    --vprint(dnsp:tostring());
    --vprint("[XX] QUESTION NAME: " .. dnsp:get_qname())
    --vprint("[XX] QUESTION TYPE: " .. dnsp:get_qtype())
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

local info_cache = {}

function add_to_cache(addr, dname, timestamp)
    ce = info_cache.addr
    if ce == nil then
      ce = {}
    end
    ce[dname] = timestamp
    info_cache[addr] = ce
end

function get_dnames_from_cache(addr)
  for a,c in pairs(info_cache) do
    vprint("[XX] CACHED: " .. a)
  end
  result = {}
  ce = info_cache[addr]
  if ce ~= nil then
    vprint("[XX] FOUND " .. addr)
    for n,t in pairs(ce) do
      vprint("[XX]   ENTRY: " .. n .. " at " .. t)
    end
    for n,_ in pairs(ce) do
      table.insert(result, n)
    end
  end
  return result
end

function print_dns_cb(mydata, event)
    if event:get_octet(21) == 53 then
      info, err = get_dns_answer_info(event)
      if info == nil then
        vprint("Error: " .. err)
      else
        for _,addr in pairs(info.ip_addrs) do
          add_to_cache(addr, info.dname, info.timestamp)
        end
        addrs_str = "[ " .. table.concat(info.ip_addrs, ", ") .. " ]"
        vprint(info.timestamp .. " " .. info.to_addr .. " " .. info.dname .. " " .. addrs_str)
        table.insert(info_cache, info)
        if table.getn(info_cache) > 10 then
          table.remove(info_cache, 0)
        end
      end
    end
end



broker = arg[1] -- defaults to "localhost" if arg not set
nl = lnflog.setup_netlogger_loop(1, print_dns_cb, mydata)
client:connect(broker)
--nl:loop_forever()
while true do
    nl:loop_once()
    client:loop()
end
nl:close()
