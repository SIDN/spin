
local util = {}

function util:capture(cmd, raw)
  local f = assert(io.popen(cmd, 'r'))
  local s = assert(f:read('*a'))
  f:close()
  if raw then return s end
  s = string.gsub(s, '^%s+', '')
  s = string.gsub(s, '%s+$', '')
  s = string.gsub(s, '[\n\r]+', ' ')
  return s
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

function util:merge_tables(a, b)
  for k,v in pairs(b) do
    a[k] = v
  end
end

return util
