--
-- Cache for nodeinfo
--

-- a node element contains the following data:
-- id
-- mac (hardware address, optional, only if known from arp)
-- ips (all ips that are known)
-- domains (all domains that are known)
--
-- The cache is mapped on id; this id is arbitrary and has no intrinsic
-- value (but we need a unique identifier)
--
local os = require 'os'

-- the module
local _M = {}


-- local helper functions
local function list_contains(list, element)
  for _,e in pairs(list) do
    if e == element then return true end
  end
  return false
end

local function list_merge(list_a, list_b)
  for _,e in pairs(list_b) do
    if not list_contains(list_a, e) then
      table.insert(list_a, e)
    end
  end
  return list_a
end


-- 'class' for one node
local Node = {}
Node.__index = Node

function Node_create()
  local n = {}
  setmetatable(n, Node)
  n.mac = nil
  n.domains = {}
  n.ips = {}
  n.lastseen = os.time()
  return n
end

function Node:has_domain(domain)
  return list_contains(self.domains, domain)
end

function Node:add_domain(domain)
  if not list_contains(self.domains, domain) then
    table.insert(self.domains, domain)
    return true
  end
  return false
end

function Node:has_ip(ip)
  return list_contains(self.ips, ip)
end

function Node:add_ip(ip)
  if not list_contains(self.ips, ip) then
    table.insert(self.ips, ip)
    return true
  end
  return false
end

function Node:set_mac(mac)
  if mac then print("[XX] SETTING MAC TO " .. mac) end
  self.mac = mac
end

function Node:get_mac(mac)
  return self.mac
end

function Node:print(out)
  out:write("- Node " .. self.id .. ":\n")
  out:write("  lastseen: " .. self.lastseen .. "\n")
  if self.mac then
    out:write("  mac: " .. self.mac .. "\n")
  else
    out:write("  mac: <no mac>\n")
  end
  for _,ip in pairs(self.ips) do
    out:write("  ip: " .. ip .. "\n")
  end
  for _,domain in pairs(self.domains) do
    out:write("  domain: " .. domain .. "\n")
  end
  out:write("- end of Node\n")
end

-- returns the raw data ready for JSON encoding
-- right now, that is simply the node itself
function Node:to_raw_data()
  return self
end

-- cache of nodes
local NodeCache = {}
NodeCache.__index = NodeCache

function _M.NodeCache_create()
  local nc = {}
  setmetatable(nc, NodeCache)
  nc.nodes = {}
  return nc
end

function NodeCache:set_dns_cache(dns_cache)
  self.dns_cache = dns_cache
end

function NodeCache:set_arp_cache(arp_cache)
  self.arp_cache = arp_cache
end

function NodeCache:create_node()
  local n = Node_create()
  table.insert(self.nodes, n)
  n.id = table.getn(self.nodes)
  return n
end

function NodeCache:get_by_id(id)
  --print("[XX] GET BY ID CALLED")
  return self.nodes[id]
end

function NodeCache:get_by_ip(ip)
  --print("[XX] GET BY IP CALLED")
  for _,n in pairs(self.nodes) do
    if n:has_ip(ip) then return n end
  end
  return nil
end

-- add a (potentially new) IP;
-- first see if we have a node with this ip yet
-- if so, return the id of that node (or the node itself?)
-- if not; create a new node. set the ip. check ARP cache for a mac
-- address. Check DNS cache for any associated domains
--
-- Returns two values:
-- id, the id of the node that was found or created
-- new, true if the node is new (no nodes known with given ip)
function NodeCache:add_ip(ip)
  local n = self:get_by_ip(ip)
  if n then return n, false end

  -- ok it's new
  n = self:create_node()
  print("[Xx] NEW NODE CREATED")
  n:add_ip(ip)
  if self.arp_cache then
    print("[Xx] try ARP")
    n:set_mac(self.arp_cache:get_hw_address(ip))
  end
  if self.dns_cache then
    print("[Xx] try DNS")
    for _,d in pairs(self.dns_cache:get_domains(ip)) do
      n:add_domain(d)
    end
  end
  -- todo: should we have another find/merge round now that
  -- we have more information about the node?
  print("[Xx] CACHE NOW")
  self:print(io.stdout)
  print("[Xx] RETURNING NEW NODE (id " .. n.id .. ")")
  return n, true
end

-- Add a domain to the given ip in the node cache
-- If this updates the cache (either ip is new or
-- domain is new for that ip), return the node
-- return nil otherwise
function NodeCache:add_domain_to_ip(ip, domain)
  local n, node_new = self:add_ip(ip)
  local domain_new = n:add_domain(domain)
  if node_new or domain_new then return n end
  return nil
end


function NodeCache:print(out)
  out:write("------- FULL NODE CACHE -------\n")
  out:write("   (containing " .. table.getn(self.nodes) .. " elements)\n")
  for _,n in pairs(self.nodes) do
    n:print(out)
  end
  out:write("----- END FULL NODE CACHE -----\n")
  out:write("\n")
  out:write("\n")
end


_M.NodeCache = NodeCache

--
-- some initial testing code
--
function test()
    local nc = _M.NodeCache_create()

    local arp = require 'arp'
    nc:set_arp_cache(arp)

    nc:print(io.stdout)
    local n1 = nc:create_node()
    local n2 = nc:create_node()
    nc:print(io.stdout)

    n1:add_ip("1.2.3.4")


    nc:print(io.stdout)

    print("[XX] ADDING (again)")
    local i = nc:add_ip("1.2.3.4")
    print("[XX] ID of node: " .. i)
    print("[XX] ADDING (new)")
    i = nc:add_ip("192.168.8.1")
    print("[XX] ID of node: " .. i)
    nc:print(io.stdout)
end

--print("cache size: " .. table.getn(nc))

return _M
