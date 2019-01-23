#!/usr/bin/lua

require "io"

function capture(cmd, single)
  local f = assert(io.popen(cmd, 'r'))
  local s = assert(f:read('*a'))
  f:close()
  if single then
    return s
  else
    return string.gmatch(s, '[^\n\r]+')
  end
end

function get_all_bound_ip_addresses()
  local s = capture("ip addr show", true)
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

function exists(filename)
    local f = io.open(filename, "r")
    if f ~= nil then
        io.close(f)
        return true
    else
        return false
    end
end

function help(rcode)
    print("usage: show_ips.lua [options]")
    print("shows the current IP addresses of this system")
    print("options:")
    print("-h           show this help")
    print("-o <file>    write output to file if it does not exist")
    print("-f           overwrite file from -o even if it does exist")
    os.exit(rcode)
end

local output_file = nil
local overwrite = false
local i
for i=1,#arg do
    local val = arg[i]
    if val == "-h" or val == "-help" then
        help(0)
    elseif val == "-o" then
        if #arg > i then
            i = i + 1
            output_file = arg[i]
            if output_file:sub(0,1) == "-" then
                help(1)
            end
        else
            help(1)
        end
    elseif val == "-f" then
        overwrite = true
    end
end

addrs = get_all_bound_ip_addresses()
if output_file then
    if exists(output_file) then
        if not overwrite then
            print("file exists, aborting")
            return
        end
    end
    local f = io.open(output_file, "w")
    for i,v in pairs(addrs) do
        f:write(i .. "\n")
    end
    io.close(f)
else
    for i,v in pairs(addrs) do
        print(i)
    end
end
