--
-- Filter and name list
-- rename this to settings?
-- This is a global list
-- When modifying, it is wise to first reload then save it immediately

--
-- format:
-- name: <address> <name>
-- ignore: <address>

local util = require 'util'
local arp = require 'arp'

local filter = {}
filter.filename = "/tmp/spin_userdata.cfg"
filter.data = {}

function filter:load(add_own_if_new)
  filter.data = {}
  filter.data.ignore = {}
  filter.data.names = {}

  -- just ignore it for now if we can't read it
  local f = io.open(filter.filename, "r")
  if not f then
    if add_own_if_new then
      -- read names from dhcp config
      filter.data.ignore = util.get_all_bound_ip_addresses()
      filter.data.names = util:read_dhcp_config_hosts("/etc/config/dhcp")
      filter:save()
    end
    return
  end

  s = f:read("*all")
  f:close()

  for address,name in string.gmatch(s, "name:%s+(%S+)%s+([^\r\n]+)") do
    filter.data.names[address] = name
  end
  for ignore in string.gmatch(s, "ignore:%s+([^\r\n]+)") do
    filter.data.ignore[ignore] = true
  end
end

function filter:save()
  -- just ignore it for now if we can't write it
  local f = io.open(filter.filename, "w")
  if not f then return end

  for address,name in pairs(filter.data.names) do
    f:write("name: " .. address .. " " .. name .. "\n")
  end
  for ignore,v in pairs(filter.data.ignore) do
    f:write("ignore: " .. ignore .. "\n")
  end
  f:close()
end

function filter:print()
  for address,name in pairs(filter.data.names) do
    print("name: " .. address .. " " .. name)
  end
  for ignore,v in pairs(filter.data.ignore) do
    print("ignore: " .. ignore .. "\n")
  end
end

function filter:add_ignore(address)
  -- address might be a mac address, if so, find the
  -- associated IPs
  local ips = arp:get_ip_addresses(address)
  if #ips > 0 then
    for i,ip in pairs(ips) do
      filter.data.ignore[ip] = true
    end
  else
    filter.data.ignore[address] = true
  end
end

function filter:remove_ignore(address)
  filter.data.ignore[address] = nil
end

function filter:add_name(address, name)
  filter.data.names[address] = name
end

function filter:get_ignore_table()
  return filter.data.ignore
end

function filter:get_ignore_list()
  local result = {}
  for k,v in pairs(filter.data.ignore) do
    table.insert(result, k)
  end
  return result
end

function filter:get_name_list()
  return filter.data.names
end

return filter
