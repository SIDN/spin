#!/usr/bin/env lua
--- lua websocket equivalent to test-server.c from libwebsockets.
-- using copas as server framework

package.path = '../src/?.lua;../src/?/?.lua;'..package.path
local copas = require'copas'
local socket = require'socket'
local P = require'posix'
local arp = require'arp'
local util = require'util'
local json = require'json'
local filter = require'filter'

local traffic_clients = {}
local info_clients = {}
local websocket = require'websocket'

filter:load(true)
print("[XX] loaded filter")
filter:print()

--
-- The traffic data protocol send out continuous updates of
-- traffic data as collected by the collector module
--
-- The info protocol is a simple async request response protocol
-- requests are in the form
-- { "command": <command>,
--   "argument": <argument> }
-- Responses get an additional field "result", with
-- data format depending on the command (usualy just a string)

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
  elseif (command == "add_name") then
    filter:load()
    filter:add_name(argument["address"], argument["name"])
    filter:save()
    -- rebroadcast names?
    response = nil
  end
  return response
end

function create_filter_list_command()
  local update  = {}
  update ["command"] = "filters"
  update ["argument"] = ""
  update ["result"] = filter:get_filter_list()
  return update
end

function send_filter_list(ws)
  local update = create_filter_list_command()
  ws:send(json.encode(update))
end

function send_name_list(ws)
  local update  = {}
  update ["command"] = "names"
  update ["argument"] = ""
  update ["result"] = filter:get_name_list()
  ws:send(json.encode(update))
end

local server = websocket.server.copas.listen
{
  protocols = {
    ['traffic-data-protocol'] = function(ws)
      traffic_clients[ws] = 0
      send_filter_list(ws)
      send_name_list(ws)

      while true do
        local msg,opcode = ws:receive()
        if not msg then
          ws:close()
          return
        else
          local response = nil
          command = json.decode(msg)
          if (command["command"] and command["argument"]) then
            print("[XX] GOT COMMAND: " .. msg)
            response = handle_command(command.command, command.argument)
            if response then
              ws:send(json.encode(response))
            end
          end
        end
      end
    end
  },
  port = 12345
}

local pid_file_name = "/tmp/TESTPIDFILE.pid"
function remove_pidfile()
    print("yo")
    --os.remove(pid_file_name)
    os.exit()
end

-- TODO: move what we can to the module

function test_print(msg)
  print(msg)
end

function create_callback(ic)
  return function(msg)
    for ws,number in pairs(ic) do
      --ws:send(msg)
      ws:send(msg)
      ic[ws] = number + 1
    end
  end
end

function send_number_loop()
    local last = socket.gettime()
    while true do
      copas.step(0.1)
      local now = socket.gettime()
      if (now - last) >= 0.1 then
        last = now
        for ws,number in pairs(traffic_clients) do
          ws:send(tostring(number))
          traffic_clients[ws] = number + 1
        end
      end
    end
end

function collector_loop()
    local fd1 = P.open("/tmp/spin_pipe", bit.bor(P.O_RDONLY, P.O_NONBLOCK))
    --local fd2 = P.open(arg[2], P.O_RDONLY)

    cb = create_callback(traffic_clients)
    --cb = test_print
    while true do
        --print("collector loop")
        copas.step(0.1)
        handle_pipe_output(fd1, cb, filter:get_filter_table())
    end
end

local collector = require'collect'

print("add thread")
copas.addthread(
collector_loop
)
print("add thread done")


copas.loop()
