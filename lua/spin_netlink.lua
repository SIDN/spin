#!/usr/bin/lua5.1

--
-- this modules allows applications to exchange data
-- with the SPIN kernel module
--
-- see README.md for information about the data exchange
-- protocol


local posix = require "posix"
--local socket = require "socket"
local wirefmt = require "wirefmt"

local _M = {}

local SPIN_NETLINK_PROTOCOL_VERSION = 1

local NETLINK_CONFIG_PORT = 30
local NETLINK_TRAFFIC_PORT = 31

_M.MAX_NL_MSG_SIZE = 1024

--
-- Netlink functions
--
function _M.create_netlink_header(payload,
                               type,
                               flags,
                               sequence,
                               pid)
    -- note: netlink headers are in the byte order of the host!
    local msg_size = string.len(payload) + 16
    return wirefmt.int32_system_endian(msg_size) ..
           wirefmt.int16_system_endian(0) ..
           wirefmt.int16_system_endian(0) ..
           wirefmt.int32_system_endian(0) ..
           wirefmt.int32_system_endian(pid)
end

-- returns a tuple of:
-- size of data
-- data sequence (as a string)
-- (type, flags, seq and pid are ignored for now)
function _M.read_netlink_message(sock_fd)
  local nlh, err, errno = posix.recv(sock_fd, 1024)
  --nlh = ss .. nlh
  --wirefmt.hexdump(nlh)
  if nlh == nil then
      print(err)
      return nil, err, errno
  end
  -- netlink headers
  local nl_size = wirefmt.bytes_to_int32_systemendian(nlh:byte(1,4))
  local nl_type = wirefmt.bytes_to_int16_systemendian(nlh:byte(5,6))
  local nl_flags = wirefmt.bytes_to_int16_systemendian(nlh:byte(7,8))
  local nl_seq = wirefmt.bytes_to_int32_systemendian(nlh:byte(9,12))
  local nl_pid = wirefmt.bytes_to_int32_systemendian(nlh:byte(13,16))
  return nlh:sub(17, nl_size)
end


--
-- Spin functions
--

_M.spin_message_types = {
    SPIN_TRAFFIC_DATA = 1,
    SPIN_DNS_ANSWER = 2,
    SPIN_BLOCKED = 3,
    SPIN_ERR_BADVERSION = 250
}

_M.spin_config_command_types = {
    -- commands from client to kernelmod
    SPIN_CMD_GET_IGNORE = 1,
    SPIN_CMD_ADD_IGNORE = 2,
    SPIN_CMD_REMOVE_IGNORE = 3,
    SPIN_CMD_CLEAR_IGNORE = 4,
    SPIN_CMD_GET_BLOCK = 5,
    SPIN_CMD_ADD_BLOCK = 6,
    SPIN_CMD_REMOVE_BLOCK = 7,
    SPIN_CMD_CLEAR_BLOCK = 8,
    SPIN_CMD_GET_EXCEPT = 9,
    SPIN_CMD_ADD_EXCEPT = 10,
    SPIN_CMD_REMOVE_EXCEPT = 11,
    SPIN_CMD_CLEAR_EXCEPT = 12,
    -- commands from kernelmod to client
    SPIN_CMD_IP = 100,
    SPIN_CMD_END = 200,
    SPIN_CMD_ERR = 400
}

local PktInfo = {}
PktInfo.__index = PktInfo

function _M.PktInfo_create()
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

function _M.DnsPktInfo_create()
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

-- read wire format packet info
-- note: this format is in network byte order
function _M.read_spin_pkt_info(data)
    local pkt_info = _M.PktInfo_create()
    pkt_info.family = data:byte(1)
    pkt_info.protocol = data:byte(2)
    pkt_info.src_addr = ""
    pkt_info.dest_addr = ""
    if (pkt_info.family == posix.AF_INET) then
        pkt_info.src_addr = wirefmt.ntop_v4(data:sub(15,18))
        pkt_info.dest_addr = wirefmt.ntop_v4(data:sub(31,34))
    elseif (pkt_info.family == posix.AF_INET6) then
        pkt_info.src_addr = wirefmt.ntop_v6(data:sub(3,18))
        pkt_info.dest_addr = wirefmt.ntop_v6(data:sub(19,34))
    end
    pkt_info.src_port = wirefmt.bytes_to_int16_bigendian(data:byte(35,36))
    pkt_info.dest_port = wirefmt.bytes_to_int16_bigendian(data:byte(37,38))
    pkt_info.payload_size = wirefmt.bytes_to_int32_bigendian(data:byte(39,42))
    pkt_info.payload_offset = wirefmt.bytes_to_int16_bigendian(data:byte(43,44))
    return pkt_info
end

function _M.read_dns_pkt_info(data)
    local dns_pkt_info = _M.DnsPktInfo_create()
    dns_pkt_info.family = data:byte(1)
    if (dns_pkt_info.family == posix.AF_INET) then
        dns_pkt_info.ip = wirefmt.ntop_v4(data:sub(14, 17))
    elseif (dns_pkt_info.family == posix.AF_INET6) then
        dns_pkt_info.ip = wirefmt.ntop_v6(data:sub(2, 17))
    end
    dns_pkt_info.ttl = wirefmt.bytes_to_int32_bigendian(data:byte(18, 21))
    local dname_size = data:byte(22)
    dns_pkt_info.dname = data:sub(23, 23 + dname_size - 2)
    return dns_pkt_info
end

-- returns 3-tuple: msg_type, msg_size, [dns_]pkt_info
function _M.parse_message(data)
    local msg_protocol_version = data:byte(1)
    if msg_protocol_version ~= SPIN_NETLINK_PROTOCOL_VERSION
        return nil, nil, nil, "Kernel protocol version mismatch"
    end
    local msg_type = data:byte(2)
    local msg_size = wirefmt.bytes_to_int16_bigendian(data:byte(3,4))
    if msg_type == _M.spin_message_types.SPIN_TRAFFIC_DATA then
        return msg_type, msg_size, _M.read_spin_pkt_info(data:sub(5))
    elseif msg_type == _M.spin_message_types.SPIN_DNS_ANSWER then
        return msg_type, msg_size, _M.read_dns_pkt_info(data:sub(5))
    elseif msg_type == _M.spin_message_types.SPIN_BLOCKED then
        return msg_type, msg_size, _M.read_spin_pkt_info(data:sub(5))
    else
        return msg_type, msg_size, nil, "unknown spin message type: " .. msg_type
    end
end

function _M.print_message(data)
    local msg_type, msg_size, pkt_info
    msg_type, msg_size, pkt_info, err = _M.parse_message(data)
    if msg_type == _M.spin_message_types.SPIN_TRAFFIC_DATA then
      io.stdout:write("[TRAFFIC] ")
    elseif msg_type == _M.spin_message_types.SPIN_DNS_ANSWER then
      io.stdout:write("[DNS] ")
    elseif msg_type == _M.spin_message_types.SPIN_BLOCKED then
      io.stdout:write("[BLOCKED] ")
    else
      print("unknown spin message type: " .. msg_type)
      return
    end
    pkt_info:print()
end

function _M.get_process_id()
    local pid = posix.getpid()
    -- can be a table or an integer
    if (type(pid) == "table") then
        return pid["pid"]
    else
        return pid
    end
end

function _M.connect_traffic()
    local fd, err = posix.socket(posix.AF_NETLINK, posix.SOCK_DGRAM, NETLINK_TRAFFIC_PORT)
    assert(fd, err)

    local ok, err = posix.bind(fd, { family = posix.AF_NETLINK,
                                     pid = 0,
                                     groups = 0 })
    assert(ok, err)
    if (not ok) then
        print("error")
        return nil, err
    end
    return fd
end

function _M.connect_config()
    local fd, err = posix.socket(posix.AF_NETLINK, posix.SOCK_RAW, NETLINK_CONFIG_PORT)
    assert(fd, err)

    local ok, err = posix.bind(fd, { family = posix.AF_NETLINK,
                                     pid = 0,
                                     groups = 0 })
    assert(ok, err)
    if (not ok) then
        print("error")
        return nil, err
    end
    return fd
end

function _M.close_connection(fd)
    posix.close(fd)
end

--
-- config command functions
--

-- send a command to the kernel module
-- cmd: one of netlink.spin_config_command_types
-- ip: ip bytestring, mandatory for add and remove, ignored for get and clear
function _M.send_cfg_command(cmd, ip)
    local response_lines = {}
    local fd = _M.connect_config()
    local msg_str = string.char(SPIN_NETLINK_PROTOCOL_VERSION)
    msg_str = msg_str .. string.char(cmd)
    if ip then
        if string.len(ip) == 4 then
            msg_str = msg_str .. string.char(posix.AF_INET) .. ip
        else
            msg_str = msg_str .. string.char(posix.AF_INET6) .. ip
        end
    end
    local hdr_str = _M.create_netlink_header(msg_str, 0, 0, 0, _M.get_process_id())

    posix.send(fd, hdr_str .. msg_str);

    while true do
        local response, err = _M.read_netlink_message(fd)
        if response == nil then
            print("Error sending command to kernel module: " .. err)
            return nil, err
        end
        local response_version = string.byte(string.sub(response, 1, 1))
        if response_version ~= SPIN_NETLINK_PROTOCOL_VERSION then
            return nil, "Kernel protocol version mismatch"
        end
        local response_type = string.byte(string.sub(response, 2, 2))
        if response_type == _M.spin_config_command_types.SPIN_CMD_IP then
            local family = string.byte(string.sub(response, 3, 3))
            local ip_str
            if family == posix.AF_INET then
                ip_str = wirefmt.ntop_v4(string.sub(response, 4, 7))
            elseif family == posix.AF_INET6 then
                ip_str = wirefmt.ntop_v6(string.sub(response, 4, 19))
            else
                print("Bad inet family: " .. family)
            end
            table.insert(response_lines, ip_str)
        elseif response_type == _M.spin_config_command_types.SPIN_CMD_END then
            -- all good, done
            break
        elseif response_type == _M.spin_config_command_types.SPIN_CMD_ERR then
            _M.close_connection(fd)
            return nil, "error from kernel module: " .. string.sub(response, 2)
        else
            _M.close_connection(fd)
            return nil, "Unknown response command type from kernel module: " .. response_type
        end
    end
    _M.close_connection(fd)
    return response_lines
end

return _M
