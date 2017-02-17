
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
    result[tokens[1]] = tokens[5]
  end
  return result
end

function util:get_arp_table()
  local result = parse_ip_neigh_output(util:capture("ip neigh", false))
  util:merge_tables(result, parse_ip_neigh_output(util:capture("ip -6 neigh", false)))
  return result
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
