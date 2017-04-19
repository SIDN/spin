local util = require 'util'
local P = require 'posix'
local arp = require 'arp'

--local filter_list = {
--  ["127.0.0.1"] = true,
--  ["::1"] = true
--}


--local filter_list = util.get_all_bound_ip_addresses()

--
-- flow information
--
local Aggregator = {}
Aggregator.__index = Aggregator

function Aggregator:create(timestamp)
  local data = {}
  data.timestamp = timestamp
  data.flows = {}
  self.data = data
  return self
end

function Aggregator:same_timestamp(timestamp)
  return self.data.timestamp == timestamp
end

function Aggregator:add_flow(from_ip, to_ip, count, size)
  local from = arp:get_hw_address(from_ip)
  if not from then
    from = from_ip
  end
  -- should we switch if to does indeed have arp?
  local to = arp:get_hw_address(to_ip)
  if not to then
    to = to_ip
  else
    --from = to
    --to = from_ip
  end
  local fdata = self.data.flows[from .. "," .. to]
  if not fdata then
    fdata = {}
    fdata.count = count
    fdata.size = size
  else
    fdata.count = fdata.count + count
    fdata.size = fdata.size + size
  end
  self.data.flows[from .. "," .. to] = fdata
end

function Aggregator:print()
  print("Timestamp: " .. self.data.timestamp)
  for c,s in pairs(self.data.flows) do
    print("c: " .. c)
  end
end

function Split(str, delim, maxNb)
    -- Eliminate bad cases...
    if string.find(str, delim) == nil then
        return { str }
    end
    if maxNb == nil or maxNb < 1 then
        maxNb = 0    -- No limit
    end
    local result = {}
    local pat = "(.-)" .. delim .. "()"
    local nb = 0
    local lastPos
    for part, pos in string.gfind(str, pat) do
        nb = nb + 1
        result[nb] = part
        lastPos = pos
        if nb == maxNb then break end
    end
    -- Handle the last field
    if nb ~= maxNb then
        result[nb + 1] = string.sub(str, lastPos)
    end
    return result
end

function flow_as_json(c, s)
  local str = '{ '
  local parts = Split(c, ",", 2)
  str = str .. '"from": ' .. parts[1] .. ', "to": ' .. parts[2] .. ', '
  str = str .. '"count": ' .. s.count .. ', '
  str = str .. '"size": ' .. s.size
  str = str .. ' }'
  return str
end

function Aggregator:json()
  local total_size = 0
  local total_count = 0
  local ft = {}
  local i = 1
  for c,s in pairs(self.data.flows) do
    ft[i] = flow_as_json(c, s)
    total_count = total_count + s.count
    total_size = total_size + s.size
    i = i + 1
  end
  local s = '{ "command": "traffic", '
  s = s .. '"argument": "",'
  s = s .. '"result": { "timestamp": ' .. self.data.timestamp .. ', '
  s = s .. '"total_count": ' .. total_count .. ', '
  s = s .. '"total_size": ' .. total_size .. ', '
  s = s .. '"flows": ['
  s = s .. table.concat(ft, ", ")
  s = s .. '] } }'

  return s
end

function Aggregator:size()
  local size = 0
  for c,s in pairs(self.data.flows) do
    size = size + 1
  end
  return size
end

function Aggregator:has_timestamp()
  return self.data.timestamp
end

local cur_aggr

function add_flow(timestamp, from, to, count, size, callback, filter_list)
  if not (filter_list[from] or filter_list[to]) then
    if not cur_aggr then
      cur_aggr = Aggregator:create(timestamp)
    end
    if cur_aggr:has_timestamp() and not cur_aggr:same_timestamp(timestamp) then
      -- todo: print intermediate empties as well?
      size = cur_aggr:size()
      if size > 0 then
        callback(cur_aggr:json())
      end
      cur_aggr = Aggregator:create(timestamp)
    else
      cur_aggr:add_flow(from, to, count, size)
    end
    --print("ts: " .. timestamp .. " from: " .. from .. " to: " .. to .. " count: " .. count .. " size: " .. size)
  else
    print("[XX] filtered out (" .. from .. " or " .. to .. ")")
  end
end

function startswith(str, part)
  return string.len(str) >= string.len(part) and str:sub(0, string.len(part)) == part
end

function handle_line(line, callback, filter_list)
  local timestamp
  local from
  local to
  local count
  local size
  for token in string.gmatch(line, "[^%s]+") do
    if not timestamp and startswith(line, "[") then
      timestamp = token:match("%d+")
    end
    if not from and startswith(token, "src=") then
      from = token:sub(5)
      --print("from: '" .. from .. "'")
    end
    if not to and startswith(token, "dst=") then
      to = token:sub(5)
    end
    if not count and startswith(token, "packets=") then
      count = tonumber(token:sub(9))
    end
    if not size and startswith(token, "bytes=") then
      size = tonumber(token:sub(7))
    end
  end
  if from and to and count and size then
    add_flow(timestamp, from, to, count, size, callback, filter_list)
  end
end


local collector = {}

function read_line_from_fd(fd)
  local result = ""
  local done = false
  while not done do
    c = P.read(fd,1)
    if not c then
      return
    end
    result = result .. c
    if c == '\n' then
      done = true
    else
      if c == '' then
        return nil
      end
    end
  end
  return result
end

function handle_pipe_output(fd, callback, clients, filter_list)
  local pr = P.rpoll(fd,10)
  if pr == 0 then
    return
  end
  str = read_line_from_fd(fd)
  while str do
    if (clients == nil) or (next(clients) ~= nil) then
        handle_line(str, callback, filter_list)
    end
    str = read_line_from_fd(fd)
  end
end

function print_cb(msg)
  print(msg)
end


return collector
--main_loop()
