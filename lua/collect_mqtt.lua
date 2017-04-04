#!/usr/bin/lua

-- This is a replacement for the named-pipe -> websocket code
-- of server.lua (which used collect.lua and websocket.lua)
--
-- It simply continually reads from the named pipe, and if there
-- is data, aggregates it an publishes it to the local mqtt server
-- on the channel CHANNEL_NAME

-- - can we do without copas?
-- - should we use mio?

-- after this is done, remove websocket.lua
-- next phase: replace conntrack with something that publishes to mqtt
-- directly, andchange this to read from mqtt too ("SPIN-traffic-raw"
-- or something)

local mqtt = require 'mosquitto'
local socket = require 'socket'
local collect = require 'collect'
local posix = require 'posix'
local filter = require'filter'
local bit = require 'bit'
local json = require 'json'
local arp = require 'arp'
local util = require 'util'

local TRAFFIC_CHANNEL = "SPIN/traffic"
local COMMAND_CHANNEL = "SPIN/commands"
local client = mqtt.new()

client.ON_CONNECT = function()
    client:subscribe(COMMAND_CHANNEL)
--        local qos = 1
--        local retain = true
--        local mid = client:publish("my/topic/", "my payload", qos, retain)
end

client.ON_PUBLISH = function()
	--client:disconnect()
end

client.ON_MESSAGE = function(mid, topic, payload)
    if topic == COMMAND_CHANNEL then
        command = json.decode(payload)
        if (command["command"] and command["argument"]) then
            print("[XX] Command: " .. payload)
            response = handle_command(command.command, command.argument)
            if response then
                local response_txt = json.encode(response)
                print("[XX] Response: " .. response_txt)
                client:publish(TRAFFIC_CHANNEL, response_txt)
            end
        end
    end
end

function create_filter_list_command()
  local update  = {}
  update ["command"] = "filters"
  update ["argument"] = ""
  update ["result"] = filter:get_filter_list()
  return update
end

function get_name_list_command()
  local update  = {}
  update ["command"] = "names"
  update ["argument"] = ""
  update ["result"] = filter:get_name_list()
  return update
end

function handle_command(command, argument)
  local response = {}
  response["command"] = command
  response["argument"] = argument
  if (command == "arp2ip") then
    local hw = argument
    local ips = arp:get_ip_addresses(hw)
    if #ips > 0 then
      response["result"] = table.concat(ips, ", ")
    else
      response = nil
    end
  elseif (command == "arp2dhcpname") then
    local dhcpdb = util:read_dhcp_config_hosts("/etc/config/dhcp")
    if dhcpdb then
      if dhcpdb[argument] then
        response["result"] = dhcpdb[argument]
      else
        response = nil
      end
    else
      response = nil
    end
  elseif (command == "ip2hostname") then
    response["result"] = util:reverse_lookup(argument)
  elseif (command == "ip2netowner") then
    response["result"] = util:whois_desc(argument)
  elseif (command == "add_filter") then
    print("[XX] GOT IGNORE COMMAND FOR " .. argument);
    -- response necessary? resend the filter list?
    filter:load()
    filter:add_filter(argument)
    filter:save()
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
  elseif (command == "get_filters") then
    print("RETURNING FILTER LIST SSSSSSSSSSSSSSS")
    response = create_filter_list_command()
  elseif (command == "add_name") then
    filter:load()
    filter:add_name(argument["address"], argument["name"])
    filter:save()
    -- rebroadcast names?
    response = nil
  elseif (command == "get_names") then
    response = get_name_list_command()
  end
  return response
end


function create_callback()
  return function(msg)
    print("[XX] publish: " .. msg)
    client:publish(TRAFFIC_CHANNEL, msg)
  end
end


function collector_loop()
    local fd1 = posix.open("/tmp/spin_pipe", bit.bor(posix.O_RDONLY, posix.O_NONBLOCK))
    --local fd2 = P.open(arg[2], P.O_RDONLY)

    --cb = test_print
    local cb = create_callback()
    while true do
        socket.sleep(0.1)
        handle_pipe_output(fd1, cb, traffic_clients, filter:get_filter_table())
        client:loop()
    end
end

filter:load(true)
broker = arg[1] -- defaults to "localhost" if arg not set
client:connect(broker)
collector_loop()
