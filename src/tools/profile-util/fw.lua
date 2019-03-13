--
--  Copyright (c) 2018 Caspar Schutijser <caspar.schutijser@sidn.nl>
-- 
--  Permission to use, copy, modify, and distribute this software for any
--  purpose with or without fee is hereby granted, provided that the above
--  copyright notice and this permission notice appear in all copies.
-- 
--  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
--  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
--  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
--  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
--  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
--  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
--  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
--

local enums = require "enums"
local util_validate = require "util_validate"

local fw = {}

local FWD_CHAIN = "forwarding_rule"

-- Assert that specified iptables chain name is in the desired format
local function validate_chain_name(chain)
	assert(string.match(chain, "^%x%x%x%x%x%x%x%x%x%x%x%x$"),
	    "invalid chain name (MAC address withouth colons): " .. chain)
end

local function mac_to_chain_name(s)
	local chain = string.gsub(s, ":", "")
	validate_chain_name(chain)
	return chain
end

-- Extract IP addresses for specified MAC address from `ip neigh` output
local function ips_for_mac(mac)
	util_validate.validate_mac(mac)
	local out = io.popen("ip neigh", "r")

	local ips = {}
	for line in out:lines() do
		if string.match(line, "lladdr " .. mac) then
			local ip = string.match(line, "^[^%s]+")
			if ip then
				util_validate.somewhat_validate_ip(ip)
				table.insert(ips, ip)
			end
		end
	end
	return ips
end

-- Perform A and AAAA lookup for specified name and return IP addresses (if any)
local function dns_lookup(name)
	-- XXX gross and dangerous!
	assert(string.match(name, "^[%w][%.%-%w]*$"), "not looking up " .. name)
	local dig = io.popen("dig +short " .. name .. " a " .. name .. "aaaa",
	    "r")

	local ips = {}
	for ip in dig:lines() do
		-- Skip names such as CNAMEs
		if not string.match(ip, "%.$") then
			util_validate.somewhat_validate_ip(ip)
			table.insert(ips, ip)
		end
	end
	return ips
end

-- Return true when specified IP address looks like an IPv6 address
local function ip_is_ip6(ip)
	util_validate.somewhat_validate_ip(ip)
	return string.find(ip, ":")
end

function fw.initialize()
	-- -F: flush
	print("iptables -F FORWARD")
	print("ip6tables -F FORWARD")
	-- -P: default policy
	print("iptables -P FORWARD DROP")
	print("ip6tables -P FORWARD DROP")
	-- -N: new
	print("iptables -N " .. FWD_CHAIN)
	print("ip6tables -N " .. FWD_CHAIN)

	-- Populate FORWARD
	print("iptables -A FORWARD -j " .. FWD_CHAIN)
	print("ip6tables -A FORWARD -j " .. FWD_CHAIN)
	print("iptables -A FORWARD -j REJECT")
	print("ip6tables -A FORWARD -j REJECT")
end

-- XXX add rule that logs dropped packets
local function generate_iptables_rule(chain, ip_from, ip_to, protocol, port,
    initiator, comment)
	assert(protocol == Protocol.TCP or protocol == Protocol.UDP)
	assert(initiator == true or initiator == false)

	if string.find(ip_from, ":") and not string.find(ip_to, ":") then
		return
	elseif not string.find(ip_from, ":") and string.find(ip_to, ":") then
		return
	end

	util_validate.somewhat_validate_ip(ip_from)
	util_validate.somewhat_validate_ip(ip_to)

	local s = ""
	if ip_is_ip6(ip_from) then
		s = s .. "ip6tables"
	else
		s = s .. "iptables"
	end
	s = s .. " -A " .. chain
	s = s .. " "
	s = s .. "-s " .. ip_from .. " -d " .. ip_to .. " "
	s = s .. "-p "
	if protocol == Protocol.TCP then
		s = s .. "tcp"
	elseif protocol == Protocol.UDP then
		s = s .. "udp"
	else
		assert(false, "protocol is neither TCP or UDP")
	end
	if initiator then
		s = s .. " --destination-port " .. port
	else
		s = s .. " --source-port " .. port
	end
	if initiator then
		s = s .. " -m state --state NEW,ESTABLISHED "
	else
		s = s .. " -m state --state ESTABLISHED "
	end
	s = s .. "-j ACCEPT"

	if comment then
		s = s .. " -m comment --comment '" .. comment .. "'"
	end

	print(s)
end

local function generate_iptables_rules(chain, device_ips, ip_to, protocol, port,
    comment)
	for _,ip_from in pairs(device_ips) do
		generate_iptables_rule(chain, ip_from, ip_to, protocol, port,
		    true, comment)
		generate_iptables_rule(chain, ip_to, ip_from, protocol, port,
		    false, comment)
	end
end

local function generate_rules_domain(chain, device_ips, dests, protocol)
	for _,dest in pairs(dests) do
		local dest_ips = dns_lookup(dest.host)
		for _,ip in pairs(dest_ips) do
			generate_iptables_rules(chain, device_ips, ip, protocol,
			    dest.port, dest.host)
		end
	end
end

local function generate_rules_ip(chain, device_ips, dests, protocol)
	for _,dest in pairs(dests) do
		generate_iptables_rules(chain, device_ips, dest.host, protocol,
		    dest.port)
	end
end

-- XXX for now, from dests, only the port is used and 0/0 can access the service
-- XXX it would be nice to at least distinguish between "local network" or
-- XXX specific IP address from the outside world
local function generate_rules_server(chain, device_ips, dests, protocol)
	for _,dest in pairs(dests) do
		for _,device_ip in pairs(device_ips) do
			generate_iptables_rules(chain, { "0/0" }, device_ip,
			    protocol, dest.port)
		end
	end
end

-- Initialize iptables chain for a device
local function initialize_device_chain(chain)
	validate_chain_name(chain)

	-- Drop
	print("iptables -t filter -D " .. FWD_CHAIN .. " -j " .. chain)
	print("ip6tables -t filter -D " .. FWD_CHAIN .. " -j " .. chain)

	-- -F: flush; delete all rules
	print("iptables -F " .. chain)
	print("ip6tables -F " .. chain)

	-- -X: delete chain
	print("iptables -X " .. chain)
	print("ip6tables -X " .. chain)

	-- -N: new chain
	print("iptables -N " .. chain)
	print("ip6tables -N " .. chain)

	print("iptables -t filter -A " .. FWD_CHAIN .. " -j " .. chain)
	print("ip6tables -t filter -A " .. FWD_CHAIN .. " -j " .. chain)
end

-- Generate iptables rules for device with specified MAC address that is
-- allowed unrestricted network access
function fw.allow_device(mac)
	local device_ips = ips_for_mac(mac)
	local chain = mac_to_chain_name(mac)

	initialize_device_chain(chain)

	assert(#device_ips > 0, "no IP addresses for device")

	for _,ip in pairs(device_ips) do
		local iptables = "iptables"
		if (ip_is_ip6(ip)) then
			iptables = "ip6tables"
		end

		local s = iptables
		s = s .. " -A " .. chain
		s = s .. " -s " .. ip
		s = s .. " -d 0/0"
		s = s .. " -j ACCEPT"
		print(s)

		s = iptables
		s = s .. " -A " .. chain
		s = s .. " -d " .. ip
		s = s .. " -s 0/0"
		s = s .. " -j ACCEPT"
		print(s)
	end
end

function fw.default_config()
	local config = {
	    use_ips_from_profile = false,
	    block_sidn = false,
	}
	return config
end

-- Generate iptables rules which enforce specified profile
function fw.generate_rules(p, config)
	local chain = mac_to_chain_name(p.mac)

	if not p.update then
		initialize_device_chain(chain)
	end

	local device_ips = {}
	if config.use_ips_from_profile then
		device_ips = p.ips
	else
		device_ips = ips_for_mac(p.mac)
	end

	assert(#device_ips > 0, "no IP addresses for device")

	generate_rules_ip(chain, device_ips, p.outgoing.tcp.ips, Protocol.TCP)
	generate_rules_domain(chain, device_ips, p.outgoing.tcp.domains,
	    Protocol.TCP)
	generate_rules_ip(chain, device_ips, p.outgoing.udp.ips, Protocol.UDP)
	generate_rules_domain(chain, device_ips, p.outgoing.udp.domains,
	    Protocol.UDP)
	generate_rules_server(chain, device_ips, p.incoming.tcp.ips,
	    Protocol.TCP)
	generate_rules_server(chain, device_ips, p.incoming.udp.ips,
	    Protocol.UDP)

	-- XXX handle p.outgoing.other, p.incoming.other
end

return fw
