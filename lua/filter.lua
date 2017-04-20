--
-- Filter and name list
-- rename this to settings?
-- This is a global list
-- When modifying, it is wise to first reload then save it immediately

--
-- format:
-- name: <address> <name>
-- filter: <address>

local util = require 'util'
local arp = require 'arp'

local filter = {}
filter.filename = "/etc/spin/spin_userdata.cfg"
filter.data = {}

function filter:load(add_own_if_new)
  filter.data = {}
  filter.data.filters = {}
  filter.data.names = {}

  -- just ignore it for now if we can't read it
  local f = io.open(filter.filename, "r")
  if not f then
    if add_own_if_new then
      -- default to own ip addresses
      filter:add_own_ips()
      -- read names from dhcp config
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
  for rfilter in string.gmatch(s, "filter:%s+([^\r\n]+)") do
    filter.data.filters[rfilter] = true
  end
end

function filter:save()
  if not filter.data.names then return end

  -- just ignore it for now if we can't write it
  local f = io.open(filter.filename, "w")
  if not f then return end

  for address,name in pairs(filter.data.names) do
    f:write("name: " .. address .. " " .. name .. "\n")
  end
  for ffilter,v in pairs(filter.data.filters) do
    f:write("filter: " .. ffilter .. "\n")
  end
  f:close()
end

function filter:print()
  for address,name in pairs(filter.data.names) do
    print("name: " .. address .. " " .. name)
  end
  for ffilter,v in pairs(filter.data.filters) do
    print("filter: " .. ffilter .. "\n")
  end
end

function filter:add_filter(address)
  -- address might be a mac address, if so, find the
  -- associated IPs
  local ips = arp:get_ip_addresses(address)
  if #ips > 0 then
    for i,ip in pairs(ips) do
      filter.data.filters[ip] = true
    end
  else
    filter.data.filters[address] = true
  end
end

function filter:add_own_ips()
  util:merge_tables(self.data.filters, util:get_all_bound_ip_addresses())
end

function filter:remove_filter(address)
  filter.data.filters[address] = nil
end

function filter:remove_all_filters()
  filter.data.filters = {}
end

function filter:add_name(address, name)
  filter.data.names[address] = name
end

function filter:get_name(address)
  return filter.data.names[address]
end

function filter:get_filter_table()
  return filter.data.filters
end

function filter:get_filter_list()
  local result = {}
  for k,v in pairs(filter.data.filters) do
    table.insert(result, k)
  end
  return result
end

function filter:get_name_list()
  return filter.data.names
end

return filter
