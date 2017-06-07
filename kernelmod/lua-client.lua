local posix = require "posix"
--local socket = require "socket"

local MAX_NL_MSG_SIZE = 2048

--
-- basic helper function to deal with
-- network and host byte order data
--

function system_littleendian()
	return string.dump(function() end):byte(7) == 1
end

function int16_bigendian(x)
  local b2=x%256  x=(x-x%256)/256
  local b1=x%256  x=(x-x%256)/256
  return string.char(b1,b2)
end

function int16_littleendian(x)
  local b2=x%256  x=(x-x%256)/256
  local b1=x%256  x=(x-x%256)/256
  return string.char(b2,b1)
end

function int16_system_endian(x)
  if system_littleendian() then
    return int16_littleendian(x)
  else
    return int16_bigendian(x)
  end
end

function int32_bigendian(x)
  local b4=x%256  x=(x-x%256)/256
  local b3=x%256  x=(x-x%256)/256
  local b2=x%256  x=(x-x%256)/256
  local b1=x%256  x=(x-x%256)/256
  return string.char(b1,b2,b3,b4)
end

function int32_littleendian(x)
  local b4=x%256  x=(x-x%256)/256
  local b3=x%256  x=(x-x%256)/256
  local b2=x%256  x=(x-x%256)/256
  local b1=x%256  x=(x-x%256)/256
  return string.char(b4,b3,b2,b1)
end

function int32_system_endian(x)
  if system_littleendian() then
    return int32_littleendian(x)
  else
    return int32_bigendian(x)
  end
end

function bytes_to_int32_bigendian(b1, b2, b3, b4)
  local result = b1
  result = 256*result + b2
  result = 256*result + b3
  result = 256*result + b4
  return result
end

function bytes_to_int32_littleendian(b1, b2, b3, b4)
  local result = b4
  result = 256*result + b3
  result = 256*result + b2
  result = 256*result + b1
  return result
end

function bytes_to_int32_systemendian(b1, b2, b3, b4)
  if system_littleendian() then
    return bytes_to_int32_littleendian(b1, b2, b3, b4)
  else
    return bytes_to_int32_bigendian(b1, b2, b3, b4)
  end
end

function bytes_to_int16_bigendian(b1, b2)
  local result = b1
  result = 256*result + b2
  return result
end

function bytes_to_int16_littleendian(b1, b2)
  local result = b2
  result = 256*result + b1
  return result
end

function bytes_to_int16_systemendian(b1, b2)
  if system_littleendian() then
    return bytes_to_int16_littleendian(b1, b2)
  else
    return bytes_to_int16_bigendian(b1, b2)
  end
end

function ntop_v4(bytes)
  return string.format("%d.%d.%d.%d", bytes:byte(1,4))
end

function printbytes(bytes)
  for b in string.gfind(bytes, ".") do
	io.write(string.format("%02X ", string.byte(b)))
  end
  io.write("\n")

  for b in string.gfind(bytes, ".") do
	io.write(string.format("%02u ", string.byte(b)))
  end
  io.write("\n")
end

--
-- Netlink functions
--
function create_netlink_header(payload, 
                               type,
                               flags,
                               sequence,
                               pid)
	-- note: netlink headers are in the byte order of the host!
	local msg_size = string.len(payload) + 16
	return int32_system_endian(msg_size) ..
	       int16_system_endian(0) ..
	       int16_system_endian(0) ..
	       int32_system_endian(0) ..
	       int32_system_endian(pid)
end

-- returns a tuple of:
-- size of data
-- data sequence (as a string)
-- (type, flags, seq and pid are ignored for now)
function read_netlink_message(sock_fd)
  -- read size first (it's in system endianness)
  local nlh, err = posix.recv(sock_fd, MAX_NL_MSG_SIZE)
  local nl_size = bytes_to_int32_systemendian(nlh:byte(1,4))
  local nl_type = bytes_to_int16_systemendian(nlh:byte(5,6))
  local nl_flags = bytes_to_int16_systemendian(nlh:byte(7,8))
  local nl_seq = bytes_to_int32_systemendian(nlh:byte(9,12))
  local nl_pid = bytes_to_int32_systemendian(nlh:byte(13,16))
  return nlh:sub(17, nl_size)
end


--
-- Spin functions
--

local spin_message_types = {
    SPIN_TRAFFIC_DATA = 1,
	SPIN_DNS_ANSWER = 2,
	SPIN_BLOCKED = 3
}

local PktInfo = {}
PktInfo.__index = PktInfo

function PktInfo_create()
  local p = {}
  setmetatable(p, PktInfo)
  p.family = nil
  p.protocol = nil
  p.src_addr = nil
  p.dest_addr = nil
  p.src_port = nil
  p.dest_port = nil
  p.payload_size = nil
  p.payload_offset = nil
  return p
end

function PktInfo:print()
    local ipv
    if self.family == posix.AF_INET then
      ipv = 4
    elseif self.family == posix.AF_INET6 then
      ipv = 6
    else
      ipv = "-unknown"
    end
	print("ipv" .. ipv ..
	      " protocol " .. self.protocol ..
	      " " .. self.src_addr ..
	      ":" .. self.src_port ..
	      " " .. self.dest_addr ..
	      ":" .. self.dest_port ..
	      " size " .. self.payload_size)
end

local DnsPktInfo = {}
DnsPktInfo.__index = DnsPktInfo

function DnsPktInfo_create()
	local d = {}
	setmetatable(d, DnsPktInfo)
	d.family = 0
	d.ip = ""
	d.ttl = 0
	d.dname = ""
	return d
end

function DnsPktInfo:print()
    io.stdout:write(self.ip)
    io.stdout:write(" ")
    io.stdout:write(self.dname)
    io.stdout:write(" ")
    io.stdout:write(self.ttl)
    io.stdout:write("\n")
end

function printbytes2(data)
    local i
    for i = 1,data:len() do
      io.stdout:write(data:byte(i) .. " ")
    end
    io.stdout:write("\n")
end

-- read wire format packet info
-- note: this format is in network byte order
function read_spin_pkt_info(data)
	local pkt_info = PktInfo_create()
	pkt_info.family = data:byte(1)
	pkt_info.protocol = data:byte(2)
	pkt_info.src_addr = ""
	pkt_info.dest_addr = ""
	if (pkt_info.family == posix.AF_INET) then
		pkt_info.src_addr = ntop_v4(data:sub(15,18))
		pkt_info.dest_addr = ntop_v4(data:sub(31,34))
	elseif (pkt_info.family == posix.AF_INET6) then
		pkt_info.src_addr = ntop_v6(data:sub(3,18))
		pkt_info.dest_addr = ntop_v6(data:sub(19,34))
	end
	pkt_info.src_port = bytes_to_int16_bigendian(data:byte(35,36))
	pkt_info.dest_port = bytes_to_int16_bigendian(data:byte(37,38))
	pkt_info.payload_size = bytes_to_int32_bigendian(data:byte(39,42))
	pkt_info.payload_offset = bytes_to_int16_bigendian(data:byte(43,44))
	return pkt_info
end

function read_dns_pkt_info(data)
	local dns_pkt_info = DnsPktInfo_create()
	dns_pkt_info.family = data:byte(1)
	if (dns_pkt_info.family == posix.AF_INET) then
		dns_pkt_info.ip = ntop_v4(data:sub(14, 17))
	elseif (dns_pkt_info.family == posix.AF_INET) then
		dns_pkt_info.ip = ntop_v6(data:sub(2, 17))
	end
	dns_pkt_info.ttl = bytes_to_int32_bigendian(data:byte(18, 21))
	local dname_size = data:byte(22)
	dns_pkt_info.dname = data:sub(23, 23 + dname_size - 1)
	return dns_pkt_info
end

function spin_read_message_type(data)
	local spin_msg_type = data:byte(1)
	local spin_msg_size = bytes_to_int16_bigendian(data:byte(2,3))
	if spin_msg_type == spin_message_types.SPIN_TRAFFIC_DATA then
	  io.stdout:write("[TRAFFIC] ")
	  local pkt_info = read_spin_pkt_info(data:sub(4))
	  pkt_info:print()
	elseif spin_msg_type == spin_message_types.SPIN_DNS_ANSWER then
	  io.stdout:write("[DNS] ")
	  local dns_pkt_info = read_dns_pkt_info(data:sub(4))
	  dns_pkt_info:print()
	elseif spin_msg_type == spin_message_types.SPIN_BLOCKED then
	  io.stdout:write("[BLOCKED] ")
	  local pkt_info = read_spin_pkt_info(data:sub(4))
	  pkt_info:print()
	else
	  print("unknown spin message type: " .. type)
	end
end


if posix.AF_NETLINK ~= nil then
	local fd, err = posix.socket(posix.AF_NETLINK, posix.SOCK_DGRAM, 31)
	assert(fd, err)

	local ok, err = posix.bind(fd, { family = posix.AF_NETLINK,
	                             --pid = posix.getpid("pid"),
	                             pid = 0,
	                             groups = 0 })
	assert(ok, err)
	if (not ok) then
		print("error")
		return
	end
	msg_str = "Hello!"
	hdr_str = create_netlink_header(msg_str, 0, 0, 0, posix.getpid("pid"))
	
	posix.send(fd, hdr_str .. msg_str);

	while true do
	    local spin_msg = read_netlink_message(fd)
	    spin_read_message_type(spin_msg)
	end
else
	print("no posix.AF_NETLINK")
end

