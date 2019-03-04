#!/usr/bin/env lua

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
local profile = require "profile"

-- http://lua.sqlite.org/index.cgi/doc/tip/doc/lsqlite3.wiki
local sqlite3 = require "lsqlite3"

local verbose = false

function usage(msg)
	if msg then
		io.stderr:write(msg .. "\n")
	end
	io.stderr:write[[
Usage: ./generate-profile.lua [-ajoPsUv] [-i ip] [-m mac] -d db
	-a:	print profile for all hosts found in the database
	-d db:	SQLite3 database file
	-i ip:	print profile for specified IP address
	-j:	JSON formatted output (usable with generate-fw.lua -e)
	-m mac:	print profile for specified MAC address
	-o:	print profile for protocols other than TCP and UDP as well
	-P:	do not filter out TCP connections with only one packet
	-s:	SPIN compat: treat every TCP/UDP port < 1024 as the server port
		and perform fuzzy timestamp matching
	-U:	do not treat every UDP port < 1024 as the server port
	-v:	verbose
]]
	os.exit(1)
end

function verbose1(msg)
	if verbose then
		io.stderr:write(msg .. "\n")
	end
end

local function sql_where_protocol(protocol)
	if protocol == Protocol.TCP then
		return "ip_proto = 6 "
	elseif protocol == Protocol.UDP then
		return "ip_proto = 17 "
	elseif protocol == Protocol.ELSE then
		return "(ip_proto != 6 and ip_proto != 17) "
	else
		assert(false, "invalid protocol")
	end
end

-- Parameters:
--  address: IP address or MAC address, e.g. "192.168.8.22" or
--           "00:17:88:71:cd:4a"
--  address_type: AddressType.IP or AddressType.MAC
--  protocol: Protocol.TCP, Protocol.UDP or Protocol.ELSE
--  comm_type: CommType.CLIENT or CommType.SERVER
--  column: Column.IP or Column.DOMAIN
--  config: the config table depending on which certain features will be
--          enabled or disabled
function get_destinations(address, address_type, protocol, comm_type, column,
    config)
	assert(address_type == AddressType.IP
	    or address_type == AddressType.MAC)
	assert(protocol == Protocol.TCP or protocol == Protocol.UDP
	    or protocol == Protocol.ELSE)
	assert(comm_type == CommType.CLIENT or comm_type == CommType.SERVER)
	assert(column == Column.IP or column == Column.DOMAIN)
	assert(config)

	local q = "select distinct "
	if column == Column.IP then
		q = q .. "ip"
	elseif column == Column.DOMAIN then
		q = q .. "domain"
	else
		assert(false)
	end
	q = q .. "_to as host, port_to as port "
	q = q .. "from annotated_flows as AF where "
	if address_type == AddressType.IP then
		q = q .. "ip"
	elseif address_type == AddressType.MAC then
		q = q .. "mac"
	else
		assert(false)
	end
	q = q .. "_"
	if comm_type == CommType.CLIENT then
		q = q .. "from"
	elseif comm_type == CommType.SERVER then
		q = q .. "to"
	else
		assert(false)
	end
	q = q .. " = $1 "
	q = q .. " and " .. sql_where_protocol(protocol)
	if not config.spincompat and protocol == Protocol.TCP then
		q = q .. "and tcp_initiated = 1 "
	end
	if comm_type == CommType.CLIENT then
		if column == Column.IP then
			q = q .. "and domain_to is null "
		elseif column == Column.DOMAIN then
			q = q .. "and domain_to is not null "
		else
			assert(false)
		end
	end
	if config.tcpg1packet and protocol == Protocol.TCP then
		q = q .. "and packets > 1 "
	end
	if (config.udpserverporthack and protocol == Protocol.UDP)
	    or (config.spincompat and
	    (protocol == Protocol.TCP or protocol == Protocol.UDP)) then
		q = q .. "and port_to < 1024 "
	end
	if protocol == Protocol.TCP or protocol == Protocol.UDP then
		q = q .. "and exists ("
		q = q .. "select id from annotated_flows as AF2 where "
		q = q .. "AF2.mac_to = AF.mac_from "
		q = q .. "and AF2.ip_to = AF.ip_from "
		q = q .. "and AF2.ip_from = AF.ip_to "
		q = q .. "and AF2.ip_proto = AF.ip_proto "
		q = q .. "and AF2.port_to = AF.port_from "
		q = q .. "and AF2.port_from = AF.port_to "
		if config.spincompat then
			-- Packets from A to B are sometimes reported later than
			-- packets flowing from B to A so don't be as strict
			-- when matching the timestamps. It's the best we can
			-- do for now.
			q = q .. "and "
			    .. "(AF.first_timestamp - 60) <= "
			    .. "AF2.first_timestamp "
			q = q .. "and AF.last_timestamp >= AF2.first_timestamp "
		else
			q = q .. "and "
			    .. "AF.first_timestamp <= AF2.first_timestamp "
			q = q .. "and AF.last_timestamp >= AF2.first_timestamp "
		end
		q = q .. ") "
	end
	q = q .. " order by host, port"

	local stmt = db:prepare(q)
	assert(stmt, db:errmsg())

	local err = stmt:bind_values(address)
	assert(err, db:errmsg())

	verbose1(q)
	verbose1("\t" .. address)

	local dests = {}
	for host, port in stmt:urows() do
		assert(host)
		assert(port)
		table.insert(dests, { host = host, port = port })
	end

	assert(stmt:finalize() == sqlite3.OK)

	return dests
end

function print_destinations(dests)
	for _,dest in pairs(dests) do
		print("- " .. dest.host .. ":" .. dest.port)
	end
end

function get_ips_for_mac(mac)
	local q = "select distinct ip_from as ip from annotated_flows "
	    .. "where mac_from == $1 "
	    .. "union select distinct ip_to as ip from annotated_flows "
	    .. "where mac_to == $1"
	local stmt = db:prepare(q)
	assert(stmt, db:errmsg())

	local err = stmt:bind_values(mac)
	assert(err, db:errmsg())

	verbose1(q)
	verbose1("\t" .. mac)

	local ips = {}
	for ip in stmt:urows() do
		assert(ip)
		table.insert(ips, ip)
	end

	assert(stmt:finalize() == sqlite3.OK)

	return ips
end

function print_ips_for_mac(mac)
	ips = get_ips_for_mac(mac)

	for _,ip in pairs(ips) do
		print("- " .. ip)
	end
end

function get_node(address, address_type, config)
	local p = profile.new()

	if address_type == AddressType.MAC then
		p.mac = address
		p.ips = get_ips_for_mac(address)
	end

	p.outgoing.tcp.ips = get_destinations(address, address_type,
	    Protocol.TCP, CommType.CLIENT, Column.IP, config)
	p.outgoing.tcp.domains = get_destinations(address, address_type,
	    Protocol.TCP, CommType.CLIENT, Column.DOMAIN, config)

	p.incoming.tcp.ips = get_destinations(address, address_type,
	    Protocol.TCP, CommType.SERVER, Column.IP, config)

	p.outgoing.udp.ips = get_destinations(address, address_type,
	    Protocol.UDP, CommType.CLIENT, Column.IP, config)
	p.outgoing.udp.domains = get_destinations(address, address_type,
	    Protocol.UDP, CommType.CLIENT, Column.DOMAIN, config)

	p.incoming.udp.ips = get_destinations(address, address_type,
	    Protocol.UDP, CommType.SERVER, Column.IP, config)

	-- XXX handle stuff other than TCP, UDP properly
	if config.otherprotos then
		p.outgoing.other.ips = get_destinations(address, address_type,
		    Protocol.ELSE, CommType.CLIENT, Column.IP, config)
		p.outgoing.other.domains = get_destinations(address,
		    address_type, Protocol.ELSE, CommType.CLIENT, Column.DOMAIN,
		    config)

		p.incoming.other.ips = get_destinations(address, address_type,
		    Protocol.ELSE, CommType.SERVER, Column.IP, config)
	end

	return p
end

function print_node(address, address_type, config)
	local p = get_node(address, address_type, config)

	if config.json then
		print(profile.to_json(p))
	else
		print("TCP outgoing:")
		print_destinations(p.outgoing.tcp.ips)
		print_destinations(p.outgoing.tcp.domains)
		print("")

		print("TCP incoming:")
		print_destinations(p.incoming.tcp.ips)
		print("")

		print("UDP outgoing:")
		print_destinations(p.outgoing.udp.ips)
		print_destinations(p.outgoing.udp.domains)
		print("")

		print("UDP incoming:")
		print_destinations(p.incoming.udp.ips)
		print("")

		-- XXX print stuff other than TCP, UDP properly
		if config.otherprotos then
			print("Other protocols outgoing:")
			print_destinations(p.outgoing.other.ips)
			print_destinations(p.outgoing.other.domains)
			print("")

			print("Other protocols incoming:")
			print_destinations(p.incoming.other.ips)
		end
	end
end

function print_all(config)
	local q = "select distinct mac_from as mac "
	    .. "from annotated_flows where mac_from != '' "
	    .. "union "
	    .. "select distinct mac_to as mac "
	    .. "from annotated_flows where mac_to != ''"

	local stmt = db:prepare(q)
	assert(stmt, db:errmsg())

	verbose1(q)

	for mac in stmt:urows() do
		assert(mac)
		if not config.json then
			print("Profile for " .. mac .. ":")
			print("IPs:")
			print_ips_for_mac(mac)
			print()
		end
		print_node(mac, AddressType.MAC, config)
		if not config.json then
			print()
			print()
			print()
		end
	end

	assert(stmt:finalize() == sqlite3.OK)
end

function main(args)
	local skip = false
	local all = false
	local address = nil
	local address_type = nil
	local db_file = nil
	local json = false
	local config = {
	    json = false,
	    otherprotos = false,
	    spincompat = false,
	    tcpg1packet = true,
	    udpserverporthack = true,
	}

	for i = 1,table.getn(args) do
		if skip then
			skip = false
		elseif args[i] == "-a" then
			if all then
				usage("cannot specify -a more than once")
			end
			all = true
		elseif args[i] == "-d" then
			if db_file then
				usage("cannot specify -d more than once")
			end
			db_file = args[i+1]
			if not db_file then
				usage("missing argument for -d")
			end
			skip = true
		elseif args[i] == "-i" then
			if address then
				usage("specify either an IP address or a MAC "
				    .. "address")
			end
			address = args[i+1]
			if not address then
				usage("missing argument for -i")
			end
			address_type = AddressType.IP
			skip = true
		elseif args[i] == "-j" then
			if config.json then
				usage("cannot specify -j more than once")
			end
			config.json = true
		elseif args[i] == "-m" then
			if address then
				usage("specify either an IP address or a MAC "
				    .. "address")
			end
			address = args[i+1]
			if not address then
				usage("missing argument for -m")
			end
			address_type = AddressType.MAC
			skip = true
		elseif args[i] == "-o" then
			if config.otherprotos then
				usage("cannot specify -o more than once")
			end
			config.otherprotos = true
		elseif args[i] == "-P" then
			if not config.tcpg1packet then
				usage("cannot specify -P more than once")
			end
			config.tcpg1packet = false
		elseif args[i] == "-s" then
			if config.spincompat then
				usage("cannot specify -s more than once")
			end
			config.spincompat = true
		elseif args[i] == "-U" then
			if not config.udpserverporthack then
				usage("cannot specify -U more than once")
			end
			config.udpserverporthack = false
		elseif args[i] == "-v" then
			if verbose then
				usage("cannot specify -v more than once")
			end
			verbose = true
		else
			usage()
		end
	end

	if not db_file then
		usage("-d must be specified")
	end
	if all and address then
		usage("incompatible arguments")
	end
	if config.spincompat and not config.udpserverporthack then
		usage("cannot specify both -s and -U")
	end
	if not all and not address then
		usage()
	end

	db = sqlite3.open(db_file, sqlite3.OPEN_READONLY)
	assert(db, "database error; does it exist?")
	assert(db:exec("PRAGMA foreign_keys = ON;"), db:errmsg())

	if address then
		print_node(address, address_type, config)
	elseif all then
		print_all(config)
	else
		assert(false)
	end
end

main(arg)

