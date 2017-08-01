local util = require 'util'
local P = require 'posix'
local arp = require 'arp'

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
  --return self.data.timestamp == timestamp
  return timestamp - self.data.timestamp < 3
end

function Aggregator:add_flow(from_ip, to_ip, from_port, to_port, count, size)
  local from = arp:get_hw_address(from_ip)
  --print("[XX] GET FROM HW ADDR: '" .. from_ip .."'")
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
  local fdata = self.data.flows[from .. ":" .. from_port .. "," .. to .. ":" .. to_port]
  if not fdata then
    fdata = {}
    fdata.count = count
    fdata.size = size
  else
    fdata.count = fdata.count + count
    fdata.size = fdata.size + size
  end
  self.data.flows[from .. ":" .. from_port .. "," .. to .. ":" .. to_port] = fdata
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
  local parts1 = Split(c, ",", 2)
  local parts2 = Split(parts1[1], ":", 2)
  local parts3 = Split(parts1[2], ":", 2)
  str = str .. '"from": ' .. parts2[1] .. ', "to": ' .. parts3[1] .. ', '
  str = str .. '"from_port": ' .. parts2[2] .. ', "to_port": ' .. parts3[2] .. ', '
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

function add_flow(timestamp, from, to, from_port, to_port, count, size, callback)
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
    cur_aggr:add_flow(from, to, from_port, to_port, count, size)
  end
  --print("ts: " .. timestamp .. " from: " .. from .. " to: " .. to .. " count: " .. count .. " size: " .. size)
end

function startswith(str, part)
  return string.len(str) >= string.len(part) and str:sub(0, string.len(part)) == part
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

function handle_pipe_output(fd, callback, clients)
  local pr = P.rpoll(fd,10)
  if pr == 0 then
    return
  end
  str = read_line_from_fd(fd)
  while str do
    if (clients == nil) or (next(clients) ~= nil) then
        handle_line(str, callback)
    end
    str = read_line_from_fd(fd)
  end
end

function print_cb(msg)
  print(msg)
end


return collector
--main_loop()
