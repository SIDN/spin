#!/usr/bin/lua

local mqtt = require 'mosquitto'
local lnflog = require 'lnflog'
local bit = require 'bit'
local json = require 'json'
local dnscache = require 'dns_cache'

local verbose = true
--
-- A simple DNS cache for answered queries
--

--
-- mqtt-related things
--
local DNS_COMMAND_CHANNEL = "SPIN/dnsnames"
local TRAFFIC_CHANNEL = "SPIN/traffic"

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
    --vprint("Get octet at " .. i)
    local labellen = event:get_octet(i)
    if bit.band(labellen, 0xc0) then
        --vprint("is dname shortcut")
        return i + 2
    end
    while labellen > 0 do
      i = i + 1 + labellen
--      vprint("Get octet at " .. i)
      labellen = event:get_octet(i)
      if bit.band(labellen, 0xc0) then
          --vprint("is dname shortcut")
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
    if not dnsp:is_response() or not (dnsp:get_qtype() == 1 or dnsp:get_qtype() == 28) then
        return nil, "DNS Packet is not an A/AAAA response packet"
    end
    --vprint(dnsp:tostring());
    --vprint("QUESTION NAME: " .. dnsp:get_qname())
    --vprint("QUESTION TYPE: " .. dnsp:get_qtype())
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

function print_dns_cb(mydata, event)
    if event:get_octet(21) == 53 then
      info, err = get_dns_answer_info(event)
      if info == nil then
        vprint("Error: " .. err)
      else
        for _,addr in pairs(info.ip_addrs) do
          --add_to_cache(addr, info.dname, info.timestamp)
          dnscache.dnscache:add(addr, info.dname, info.timestamp)
          dnscache.dnscache:clean(info.timestamp - 10)
        end
        addrs_str = "[ " .. table.concat(info.ip_addrs, ", ") .. " ]"
        vprint("Event: " .. info.timestamp .. " " .. info.to_addr .. " " .. info.dname .. " " .. addrs_str)
      end
    end
end


vprint("SPIN experimental DNS capture tool")
broker = arg[1] -- defaults to "localhost" if arg not set
nl = lnflog.setup_netlogger_loop(1, print_dns_cb, mydata)
vprint("Connecting to broker")
client:connect(broker)
--nl:loop_forever()
vprint("Starting listen loop")
while true do
    nl:loop_once()
    client:loop()
end
nl:close()
