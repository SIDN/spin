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

function _M.ntop_v6(bytestring)
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

function _M.pton_v6(str)
  -- first, split the string into the two parts separated by ::
  local parts = split_string(str, "::")
  -- must be of length one or two
  if #parts == 0 or #parts > 2 then
    return nil
  end
  local head = parts[1]
  local tail = parts[2]
  -- split parts up into hex blocks
  local head_bytes = ""
  local tail_bytes = ""
  for _,hex_str in pairs(split_string(head, ":")) do
    if string.len(hex_str) > 4 then
      return nil
    else
      while string.len(hex_str) < 4 do
        hex_str = "0" .. hex_str
      end
    end
    head_bytes = head_bytes .. string.char(tonumber(string.sub(hex_str, 1, 2), 16))
    head_bytes = head_bytes .. string.char(tonumber(string.sub(hex_str, 3, 4), 16))
  end
  for _,hex_str in pairs(split_string(tail, ":")) do
    if string.len(hex_str) > 4 then
      return nil
    else
      while string.len(hex_str) < 4 do
        hex_str = "0" .. hex_str
      end
    end
    tail_bytes = tail_bytes .. string.char(tonumber(string.sub(hex_str, 1, 2), 16))
    tail_bytes = tail_bytes .. string.char(tonumber(string.sub(hex_str, 3, 4), 16))
  end
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

return _M
