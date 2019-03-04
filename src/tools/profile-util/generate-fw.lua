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

local fw = require "fw"
local profile = require "profile"

function usage(msg)
	if msg then
		io.stderr:write(msg .. "\n")
	end
	io.stderr:write[[
Usage: ./generate-fw.lua [-eip] [-a mac]
	-a:	do not enforce anything for specified MAC address
	-e:	enforce profile provided on stdin
	-i:	initialize firewall
	-p:	use IP address from profile rather than querying `ip neigh`
]]
	os.exit(1)
end

function main(args)
	local skip = false
	local allow = nil
	local enforce = false
	local initialize = false

	local config = fw.default_config()

	for i = 1, table.getn(args) do
		if skip then
			skip = false
		elseif args[i] == "-a" then
			if allow then
				usage("cannot specify -a more than once")
			end
			allow = args[i+1]
			if not allow then
				usage("missing argument for -a")
			end
			skip = true
		elseif args[i] == "-e" then
			if enforce then
				usage("cannot specify -e more than once")
			end
			enforce = true
		elseif args[i] == "-i" then
			if initialize then
				usage("cannot specify -i more than once")
			end
			initialize = true
		elseif args[i] == "-p" then
			if config.use_ips_from_profile then
				usage("cannot specify -p more than once")
			end
			config.use_ips_from_profile = true
		else
			usage()
		end
	end

	if (allow and enforce) or (allow and initialize)
	    or (enforce and initialize) then
		usage("incompatible arguments")
	end

	if not allow and not enforce and not initialize then
		usage("must specify either -a, -e or -i")
	end

	if not enforce and config.use_ips_from_profile then
		usage("-p can only be specified with -e")
	end

	if allow then
		fw.allow_device(allow)
	elseif enforce then
		for line in io.lines() do
			p = profile.from_json(line)
			fw.generate_rules(p, config)
		end
	elseif initialize then
		fw.initialize()
	else
		assert(false)
	end
end

main(arg)

