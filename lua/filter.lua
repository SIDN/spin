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
local netlink = require 'spin_netlink'
local wirefmt = require 'wirefmt'


local filter = {}
filter.filename = "/etc/spin/spin_userdata.cfg"
filter.data = {}

function filter:load(add_own_if_new)
  filter.data = {}
  filter.data.filters = {}
  filter.data.blocks = {}
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
  for rfilter in string.gmatch(s, "block:%s+([^\r\n]+)") do
    filter.data.blocks[rfilter] = true
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
  for block,v in pairs(filter.data.blocks) do
    f:write("block: " .. block .. "\n")
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
  for block,v in pairs(filter.data.blocks) do
    print("filter: " .. blocks .. "\n")
  end
end

function filter:add_filter(address)
  -- address might be a mac address, if so, find the
  -- associated IPs
  local ips = arp:get_ip_addresses(address)
  if #ips > 0 then
    for i,ip in pairs(ips) do
      -- relay back any errors?
      self:send_cmd_to_kernel(netlink.spin_config_command_types.SPIN_CMD_ADD_IGNORE, ip)
      filter.data.filters[ip] = true
    end
  else
    self:send_cmd_to_kernel(netlink.spin_config_command_types.SPIN_CMD_ADD_IGNORE, address)
    filter.data.filters[address] = true
  end
end

function filter:add_block(address)
  -- address might be a mac address, if so, find the
  -- associated IPs
  local ips = arp:get_ip_addresses(address)
  if #ips > 0 then
    for i,ip in pairs(ips) do
      self:send_cmd_to_kernel(netlink.spin_config_command_types.SPIN_CMD_ADD_BLOCK, ip)
      filter.data.blocks[ip] = true
    end
  else
    self:send_cmd_to_kernel(netlink.spin_config_command_types.SPIN_CMD_ADD_BLOCK, address)
    filter.data.blocks[address] = true
  end
end

function filter:add_own_ips()
  util:merge_tables(self.data.filters, util:get_all_bound_ip_addresses())
end

function filter:remove_filter(ip)
  self:send_cmd_to_kernel(netlink.spin_config_command_types.SPIN_CMD_REMOVE_IGNORE, ip)
  filter.data.filters[ip] = nil
end

function filter:remove_block(ip)
  self:send_cmd_to_kernel(netlink.spin_config_command_types.SPIN_CMD_REMOVE_BLOCK, ip)
  filter.data.blocks[ip] = nil
end

function filter:remove_all_filters()
  netlink.send_cfg_command(netlink.spin_config_command_types.SPIN_CMD_CLEAR_IGNORE)
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

function filter:send_cmd_to_kernel(cmd, ip)
    ip_bytes = wirefmt.pton(ip)
    if ip_bytes then
        return netlink.send_cfg_command(cmd, ip_bytes)
    end
    return nil, "Bad IP address: " .. ip
end

-- assumes filter:load() is called first!
-- clears all filters and all blocks from the kernel, and replaces
-- them with the values we have (either user-set or read from config)
function filter:apply_current_to_kernel()
  netlink.send_cfg_command(netlink.spin_config_command_types.SPIN_CMD_CLEAR_IGNORE)
  for ip,_ in pairs(self.data.filters) do
    print("[XX] TRY " .. ip)
    self:send_cmd_to_kernel(netlink.spin_config_command_types.SPIN_CMD_ADD_IGNORE, ip)
  end
  netlink.send_cfg_command(netlink.spin_config_command_types.SPIN_CMD_CLEAR_BLOCK)
  for ip,_ in pairs(self.data.blocks) do
    self:send_cmd_to_kernel(netlink.spin_config_command_types.SPIN_CMD_ADD_BLOCK, ip)
  end
end

return filter
