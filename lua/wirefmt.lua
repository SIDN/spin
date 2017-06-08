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

function _M.ntop_v4(bytes)
  return string.format("%d.%d.%d.%d", bytes:byte(1,4))
end

function _M.ntop_v6(bytes)
  return string.format("%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x", bytes:byte(1,16))
end

return _M
