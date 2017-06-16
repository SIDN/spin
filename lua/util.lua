
local wirefmt = require "wirefmt"
local util = {}

--
-- External process utility functions
--

-- if single is true, the entire output is returned as one value
-- if single is false, an iterator is returned that returns one
-- non-empty line per call
function util:capture(cmd, single)
  local f = assert(io.popen(cmd, 'r'))
  local s = assert(f:read('*a'))
  f:close()
  if single then
    return s
  else
    return string.gmatch(s, '[^\n\r]+')
  end
end

function util:get_all_bound_ip_addresses()
  local s = util:capture("ip addr show", true)
  local result = {}
  result["0.0.0.0"] = true
  for l in string.gmatch(s, "inet %d+.%d+.%d+.%d+") do
    result[l:sub(6)] = true
  end
  for l in string.gmatch(s, "inet6 [%da-f:]+") do
    result[l:sub(7)] = true
  end
  return result
end

function parse_ip_neigh_output(s)
  local result = {}
  for l in s do
    local tokens = util:line_to_tokens(l)
    -- normalize the ip address
    result[wirefmt.ntop(wirefmt.pton(tokens[1]))] = tokens[5]
  end
  return result
end

function util:get_arp_table()
  local result = parse_ip_neigh_output(util:capture("ip neigh", false))
  util:merge_tables(result, parse_ip_neigh_output(util:capture("ip -6 neigh", false)))
  return result
end

-- reads the dhcp config file (if it exists)
-- returns a table of mac->name (nil if not set)
-- returns nil if file not found
function util:read_dhcp_config_hosts(filename)
  local result = {}
  local f = io.open(filename, "r")
  if not f then return result end
  s = f:read("*all")
  f:close()
  -- err, we probably need a somewhat decent parser here; the order is not fixed
  for name,hw in string.gmatch(s, "config host%s+option name '(%S+)'%s+option mac '(%S+)'") do
    result[hw] = name
  end
  return result
end

-- Calls unbound-host to do a reverse lookup
-- returns the domain name if found, or nil if not
-- (TODO: also try host, bind-host and knot-host?)
function util:reverse_lookup(address)
  local s = util:capture("unbound-host " .. address, true)
  for token in string.gmatch(s, "domain name pointer (%S+)") do
    return token
  end
  return "No reverse name found"
end

-- calls the whois command and returns the *first* descr: line value
function util:whois_desc(address)
  local s = util:capture("whois " .. address, true)
  for token in string.gmatch(s, "OrgName:%s+([^\r\n]+)") do
    return token
  end
  for token in string.gmatch(s, "descr:%s+([^\r\n]+)") do
    return token
  end
  return "Not found"
end

--
-- Assorted basic utility functions
--
function util:merge_tables(a, b)
  for k,v in pairs(b) do
    a[k] = v
  end
end

function util:line_to_tokens(line)
  local result = {}
  for token in string.gmatch(line, "%S+") do
    table.insert(result, token)
  end
  return result
end

return util
