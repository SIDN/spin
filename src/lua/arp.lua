--
-- ARP lookup functions
--

local util = require 'util'
local wirefmt = require 'wirefmt'

local arp = {}

-- arp table contents:
-- <ip address> -> { 'hw': <mac address>, 'device': <iface> }

function arp:get_hw_address(ip_address)
  -- load if not loaded, or if ip is not in currently loaded table
  -- normalize the ip address
  ip_address = wirefmt.ntop(wirefmt.pton(ip_address))
  if not arp.arptable or not arp.arptable[ip_address] then
    arp.arptable = util:get_arp_table()
  end
  if arp.arptable[ip_address] then
    return arp.arptable[ip_address]
  else
    return nil
  end
end

function arp:get_ip_addresses(hw_address)
  local result = {}
  if not arp.arptable or not arp.arptable[ip_address] then
    arp.arptable = util:get_arp_table()
  end
  for k,v in pairs(arp.arptable) do
    if v == hw_address then
      table.insert(result, k)
    end
  end
  return result
end

function arp:print_table()
  print("ARP TABLE:")
  if not arp.arptable then
    print("<not read>")
  else
    for k,v in pairs(arp.arptable) do
      print(k .. ": " .. v)
    end
  end
end

return arp;
