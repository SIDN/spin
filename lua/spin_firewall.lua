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

local config = require 'config'


-- just some testing code

cfg = config.create_Config("tests/testdata/firewall1.config")
for i,v in pairs(cfg) do
  print(i ..": " .. type(v))
end

cfg:read_config()
--cfg:print()

-- interpret and parse config
local s, err = cfg:get_section_by_option_value("rule", "proto", "udp")
if s == nil then
  print("Error: " .. err)
else
  for _,p in pairs(s) do
    p:print()
  end
end
