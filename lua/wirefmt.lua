--
-- this modules provides a few functions to convert
-- from and to wireformat, such as reading big-endian
-- and little-endian numbers, and IP addresses
--

local _M = {}

-- returns true if the current system is little-endian
--         false otherwise
function _M.system_littleendian()
    return string.dump(function() end):byte(7) == 1
end

-- returns the given 16-bit integer as a big-endian byte-string
function _M.int16_bigendian(x)
  local b2=x%256  x=(x-x%256)/256
  local b1=x%256  x=(x-x%256)/256
  return string.char(b1,b2)
end

-- returns the given 16-bit integer as a little-endian byte-string
function _M.int16_littleendian(x)
  local b2=x%256  x=(x-x%256)/256
  local b1=x%256  x=(x-x%256)/256
  return string.char(b2,b1)
end

-- returns the given 16-bit integer as a byte-string with the
-- endianness of the system
function _M.int16_system_endian(x)
  if _M.system_littleendian() then
    return _M.int16_littleendian(x)
  else
    return _M.int16_bigendian(x)
  end
end

function _M.int32_bigendian(x)
  local b4=x%256  x=(x-x%256)/256
  local b3=x%256  x=(x-x%256)/256
  local b2=x%256  x=(x-x%256)/256
  local b1=x%256  x=(x-x%256)/256
  return string.char(b1,b2,b3,b4)
end

function _M.int32_littleendian(x)
  local b4=x%256  x=(x-x%256)/256
  local b3=x%256  x=(x-x%256)/256
  local b2=x%256  x=(x-x%256)/256
  local b1=x%256  x=(x-x%256)/256
  return string.char(b4,b3,b2,b1)
end

function _M.int32_system_endian(x)
  if _M.system_littleendian() then
    return _M.int32_littleendian(x)
  else
    return _M.int32_bigendian(x)
  end
end

function _M.bytes_to_int32_bigendian(b1, b2, b3, b4)
  local result = b1
  result = 256*result + b2
  result = 256*result + b3
  result = 256*result + b4
  return result
end

function _M.bytes_to_int32_littleendian(b1, b2, b3, b4)
  local result = b4
  result = 256*result + b3
  result = 256*result + b2
  result = 256*result + b1
  return result
end

function _M.bytes_to_int32_systemendian(b1, b2, b3, b4)
  if _M.system_littleendian() then
    return _M.bytes_to_int32_littleendian(b1, b2, b3, b4)
  else
    return _M.bytes_to_int32_bigendian(b1, b2, b3, b4)
  end
end

function _M.bytes_to_int16_bigendian(b1, b2)
  local result = b1
  result = 256*result + b2
  return result
end

function _M.bytes_to_int16_littleendian(b1, b2)
  local result = b2
  result = 256*result + b1
  return result
end

function _M.bytes_to_int16_systemendian(b1, b2)
  if _M.system_littleendian() then
    return _M.bytes_to_int16_littleendian(b1, b2)
  else
    return _M.bytes_to_int16_bigendian(b1, b2)
  end
end

function _M.ntop_v4(bytestring)
  return string.format("%d.%d.%d.%d", bytestring:byte(1,4))
end

local function strjoin(delimiter, list)
   local len = 0
   if list then len = table.getn(list) end
   if len == 0 then
      return ""
   elseif len == 1 then
      return list[1]
   else
     local string = list[1]
     for i = 2, len do
        string = string .. delimiter .. list[i]
     end
     return string
   end
end

function _M.ntop_v6(bytestring)
  -- split the address into 8 groups
  local parts = {}
  local i
  -- get the number value of each field
  for i=1,16,2 do
    local n = 256*bytestring:byte(i) + bytestring:byte(i+1)
    table.insert(parts, n)
  end
  -- replace the first repeating occurrence of zeroes with ::
  -- okay this can probably be done better
  local repl_done = false
  local parts2 = {}
  local i = 1
  while i <= #parts do
    p = parts[i]
    if not repl_done and p == 0 and parts[i+1] == 0 then
      while parts[i+1] == 0 do
        i = i + 1
      end
      table.insert(parts2, "")
      repl_done = true
    else
      table.insert(parts2, string.format("%x", p))
    end
    i = i + 1
  end
  if parts2[1] == "" then parts2[1] = ":" end
  if parts2[#parts2] == "" then parts2[#parts2] = ":" end

  return strjoin(":", parts2)
end

function _M.ntop(bytestring)
  if bytestring == nil then return nil end
  if string.len(bytestring) == 16 then
    return _M.ntop_v6(bytestring)
  else
    return _M.ntop_v4(bytestring)
  end
end

function _M.old_ntop_v6(bytestring)
  return string.format("%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x", bytestring:byte(1,16))
end

-- returns bytestring of ip address, if valid, nil otherwise
function _M.pton_v4(str)
  local ret = ""
  for c in string.gmatch(str, "[0-9]+") do
    if (c+0 > 255) then
        return nil
    end
    ret = ret .. string.char(c+0)
  end
  if string.len(ret) ~= 4 then
      return nil
  end
  return ret  
end

local function split_string(str, sep)
  local result = {}
  if not str then return result end
  while true do
      local s,e = string.find(str, sep)
      if s then
        table.insert(result, str:sub(1, s - 1))
        str = str:sub(e + 1)
      else
        break
      end
  end
  if string.len(str) > 0 then
    table.insert(result, str)
  end
  return result
end

local function v6_part_bytes(str)
  bytes = ""
  for _,hex_str in pairs(split_string(str, ":")) do
    if string.len(hex_str) > 4 then
      return nil
    else
      while string.len(hex_str) < 4 do
        hex_str = "0" .. hex_str
      end
    end
    local num1 = tonumber(string.sub(hex_str, 1, 2), 16)
    if num1 == nil then return nil end
    local num2 = tonumber(string.sub(hex_str, 3, 4), 16)
    if num2 == nil then return nil end
    bytes = bytes .. string.char(num1) .. string.char(num2)
  end
  return bytes
end

-- returns bytestring of ipv6 address, if valid, nil otherwise
function _M.pton_v6(str)
  -- first, split the string into the two parts separated by ::
  local parts = split_string(str, "::")
  -- must be of length one or two
  if #parts == 0 or #parts > 2 then
    return nil
  end
  -- convert the two parts into bytestrings
  local head_bytes = v6_part_bytes(parts[1])
  if head_bytes == nil then return nil end
  local tail_bytes = v6_part_bytes(parts[2])
  if head_bytes == nil then return nil end
  local i
  -- if there was no :: the length must be 16; otherwise fill it up
  local cur_size = string.len(head_bytes) + string.len(tail_bytes)
  if #parts == 2 then
    if cur_size > 15 then return nil end
    for i=1,16 - string.len(head_bytes) - string.len(tail_bytes) do
      head_bytes = head_bytes .. string.char(0)
    end
  else
    if cur_size ~= 16 then
      return nil
    end
  end
  return head_bytes .. tail_bytes
end

function _M.pton(ip)
    local bytes = _M.pton_v6(ip)
    if not bytes then bytes = _M.pton_v4(ip) end
    return bytes
end

function _M.hexdump(data)
    local i
    io.stdout:write("00: ")
    for i=1,#data do
        if (i>1 and (i-1)%10 == 0) then
          io.stdout:write(string.format("\n%2d: ", i-1))
        end
        io.stdout:write(string.format("%02x ", string.byte(data:sub(i))))
    end
    io.stdout:write("\n")
end

return _M
