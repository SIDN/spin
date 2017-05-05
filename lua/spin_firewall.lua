#!/usr/bin/lua

--
-- Module to add and remove SPIN-specific firewall rules
--

-- for starters, once SPIN starts, we need NFLOG entries
-- so we need to add them if the ydo not exist
--
-- Secondly, users can block devices and addresses (maybe ranges)
-- so we need some code to apply those rules
--
-- Rather than talking directly to the firewall, this modules
-- uses the OpenWRT firewall configuration file. That way, administrators
-- can always tweak or remove rules set by SPIN, when necessary.
--

local mio = require 'mio'

local _M = {}

local SpinFW = {}
SpinFW.__index = SpinFW

function _M.SpinFW_create(ipv6)
  local sfw = {}
  setmetatable(sfw, SpinFW)
  -- logging rules
  sfw.initial = {}
  sfw.allow = {}
  sfw.reject = {}
  sfw.final = {}
  sfw.ipv6 = ipv6
  if ipv6 then
    sfw.command = "ip6tables"
    sfw.config_file = "/etc/spin/firewall6.conf"
  else
    sfw.command = "iptables"
    sfw.config_file = "/etc/spin/firewall.conf"
  end
  return sfw
end

function startswith(str, part)
  return string.len(str) >= string.len(part) and str:sub(0, string.len(part)) == part
end

function SpinFW:make_inital_rules()
    return {
      "# SPIN Firewall module configuration file",
      "# Set and manipulated by the SPIN daemon",
      "# Do not manually edit",
      "",
      self.command .. " -N SPIN_CHECK",
      self.command .. " -N SPIN_DNS",
      self.command .. " -N SPIN_BLOCKED",
      "",
      self.command .. " -I FORWARD 1 -j SPIN_CHECK",
      self.command .. " -A SPIN_CHECK -j NFLOG --nflog-group 771",
      "",
      -- DNS is output from ourselves
      self.command .. " -I OUTPUT 1 -p tcp --source-port 53 -j SPIN_DNS",
      self.command .. " -I OUTPUT 1 -p udp --source-port 53 -j SPIN_DNS",
      "",
      self.command .. " -A SPIN_DNS -j NFLOG --nflog-group 772",
      self.command .. " -A SPIN_DNS -j RETURN",
      "",
      self.command .. " -A SPIN_BLOCKED -j NFLOG --nflog-group 773",
      self.command .. " -A SPIN_BLOCKED -j REJECT",
      "",
      "",
      -- temp test rules as example
      self.command .. " -A SPIN_CHECK -j RETURN",
      "",
    }
end

function SpinFW:read()
  local cfr, err = mio.file_reader(self.config_file)
  if cfr == nil then
    io.stderr:write("Unable to open " .. self.config_file .. ": " .. err .. "\n")
    io.stderr:write("Creating new SPIN firewall config\n")
    self.initial = default_initial_rules
  else
    local state = 1
    for line in cfr:read_line_iterator(true) do
      if startswith(line, "# SPIN_ALLOW") then state = 2
      elseif startswith(line, "# SPIN_REJECT") then state = 3
      elseif startswith(line, "# SPIN_END") then state = 4
      else
        if state == 1 then
          table.insert(self.initial, line)
        elseif state == 2 then
          --if line:match(self.command .. " -A SPIN_CHECK -d 10.1.2.3 -j RETURN") then
          addr = line:match(self.command .. " %-A SPIN_CHECK %-d (%S+) %-j RETURN")
          if addr then
            table.insert(self.allow, addr)
          end
          -- todo read the ip here
        elseif state == 3 then
          addr = line:match(self.command .. " %-I SPIN_CHECK 1 %-d (%S+) %-j SPIN_BLOCKED")
          if addr then
            table.insert(self.reject, addr)
          end
          -- todo read the ip here
        elseif state == 4 then
          table.insert(self.final, line)
        else
          return nil, "Error, unknown state reached"
        end
      end
    end
  end
end



function SpinFW:write(out)
  for _,l in pairs(self.initial) do
    out:write(l .. "\n")
  end
  out:write("# SPIN_ALLOW Specifically allowed addresses go below\n")
  for _,l in pairs(self.allow) do
    out:write(self.command .. " -A SPIN_CHECK -d " .. l .. " -j RETURN\n")
  end
  out:write("# SPIN_REJECT Specifically denied addresses go below\n")
  for _,l in pairs(self.reject) do
    out:write(self.command .. " -I SPIN_CHECK 1 -s " .. l .. " -j SPIN_BLOCKED\n")
    out:write(self.command .. " -I SPIN_CHECK 1 -d " .. l .. " -j SPIN_BLOCKED\n")
  end
  out:write("# SPIN_END End of managed entries\n")
  for _,l in pairs(self.final) do
    out:write(l .. "\n")
  end
end

function SpinFW:add_block_ip(ip)
  for _,v in pairs(self.reject) do
    if v == ip then return end
  end
  table.insert(self.reject, ip)
end

function SpinFW:remove_block_ip(ip)
  local di = nil
  for i,v in pairs(self.reject) do
    if v == ip then
      di = i
    end
  end
  if di then table.remove(self.reject, di) end
end

function SpinFW:save()
  local writer, err = io.open(self.config_file, "w")
  if not writer then
    error(err)
  end
  for _,l in pairs(self.initial) do
    writer:write(l .. "\n")
  end
  writer:write("# SPIN_ALLOW Specifically allowed addresses go below\n")
  for _,l in pairs(self.allow) do
    writer:write(self.command .. " -A SPIN_CHECK -d " .. l .. " -j RETURN\n")
  end
  writer:write("# SPIN_REJECT Specifically denied addresses go below\n")
  for _,l in pairs(self.reject) do
    writer:write(self.command .. " -I SPIN_CHECK 1 -d " .. l .. " -j SPIN_BLOCKED\n")
    writer:write(self.command .. " -I SPIN_CHECK 1 -s " .. l .. " -j SPIN_BLOCKED\n")
  end
  writer:write("# SPIN_END End of managed entries\n")
  for _,l in pairs(self.final) do
    writer:write(l .. "\n")
  end
  writer:close()
end

function SpinFW:commit()
  self:save()
  mio.execute("/etc/init.d/firewall restart")
end

--local s = _M.SpinFW_create()
--s:read()
--s:write(io.stdout)
--s:commit()

return _M
