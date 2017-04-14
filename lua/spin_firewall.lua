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

local FIREWALL_RULES_CONFIG = "/home/jelte/myvalibox/etc/spin/firewall.conf"
--local FIREWALL_RULES_CONFIG = "/etc/spin/firewall.conf"

local default_initial_rules = {
  "# SPIN Firewall module configuration file",
  "# Set and manipulated by the SPIN daemon",
  "# Do not manually edit",
  "",
  "iptables -N SPIN_CHECK",
  "iptables -N SPIN_DNS",
  "iptables -N SPIN_BLOCKED",
  "",
  "iptables -I FORWARD 1 -j SPIN_CHECK",
  "iptables -A SPIN_CHECK -j NFLOG --nflog-group 771",
  "",
  -- DNS is output from ourselves
  "iptables -I OUTPUT 1 -p tcp --source-port 53 -j SPIN_DNS",
  "iptables -I OUTPUT 1 -p udp --source-port 53 -j SPIN_DNS",
  "",
  "iptables -A SPIN_DNS -j NFLOG --nflog-group 772",
  "iptables -A SPIN_DNS -j RETURN",
  "",
  "iptables -A SPIN_BLOCKED -j NFLOG --nflog-group 773",
  "iptables -A SPIN_BLOCKED -j REJECT",
  "",
  "",
  -- temp test rules as example
  "iptables -A SPIN_CHECK -d 10.1.2.3 -j RETURN",
  "iptables -A SPIN_CHECK -d 10.1.2.0/24 -j SPIN_BLOCKED",
  "iptables -A SPIN_CHECK -j RETURN",
  "",
}

local SpinFW = {}
SpinFW.__index = SpinFW

function _M.SpinFW_create()
  local sfw = {}
  setmetatable(sfw, SpinFW)
  -- logging rules
  sfw.initial = {}
  sfw.allow = {}
  sfw.reject = {}
  sfw.final = {}
  return sfw
end

function startswith(str, part)
  return string.len(str) >= string.len(part) and str:sub(0, string.len(part)) == part
end

function SpinFW:read()
  local cfr, err = mio.file_reader(FIREWALL_RULES_CONFIG)
  if cfr == nil then
    io.stderr:write("Unable to open " .. FIREWALL_RULES_CONFIG .. ": " .. err .. "\n")
    io.stderr:write("Creating new SPIN firewall config\n")
    self.initial = default_initial_rules
  else
    local state = 1
    for line in cfr:read_line_iterator(true) do
      print("[XX] state " .. state)
      if startswith(line, "# SPIN_ALLOW") then state = 2
      elseif startswith(line, "# SPIN_REJECT") then state = 3
      elseif startswith(line, "# SPIN_END") then state = 4
      else
        if state == 1 then
          table.insert(self.initial, line)
        elseif state == 2 then
          --if line:match("iptables -A SPIN_CHECK -d 10.1.2.3 -j RETURN") then
          addr = line:match("iptables %-A SPIN_CHECK %-d (%S+) %-j RETURN")
          if addr then
            table.insert(self.allow, addr)
          end
          -- todo read the ip here
        elseif state == 3 then
          addr = line:match("iptables %-A SPIN_CHECK %-d (%S+) %-j SPIN_BLOCKED")
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
    out:write("iptables -A SPIN_CHECK -d " .. l .. " -j RETURN\n")
  end
  out:write("# SPIN_REJECT Specifically denied addresses go below\n")
  for _,l in pairs(self.reject) do
    out:write("iptables -A SPIN_CHECK -d " .. l .. " -j SPIN_BLOCKED\n")
  end
  out:write("# SPIN_END End of managed entries\n")
  for _,l in pairs(self.final) do
    out:write(l .. "\n")
  end
end

local s = _M.SpinFW_create()
s:read()
s:write(io.stdout)

return _M
