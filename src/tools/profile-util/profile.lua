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

local util_validate = require "util_validate"

local json = require "json"

local profile = {}

function profile.new()
	local p = {
	    mac = "",
	    ips = {},
	    update = false, -- new profile or update of existing profile?
	    outgoing = {
	        tcp = {
	            ips = {},
	            domains = {}
                },
	        udp = {
	            ips = {},
	            domains = {}
                },
	        other = {
	            ips = {},
	            domains = {}
	        }
	    },
	    incoming = {
	        tcp = {
	            ips = {}
                },
	        udp = {
	            ips = {}
                },
	        other = {
	            ips = {}
	        }
	    }
	}
	profile.verify(p)
	return p
end

local function verify_dests(dests)
	assert(dests)
	for _,dest in pairs(dests) do
		assert(dest.host)
		assert(dest.port)
		assert(string.match(dest.port, "^%d+$"),
		    "invalid port: " .. dest.port)
	end
end

function profile.verify(p)
	assert(p.mac)
	if (p.mac ~= "") then
		util_validate.validate_mac(p.mac)
	end
	assert(p.ips)
	for _,ip in pairs(p.ips) do
		util_validate.somewhat_validate_ip(ip)
	end
	assert(p.update == false or p.update == true)
	verify_dests(p.outgoing.tcp.ips)
	verify_dests(p.outgoing.tcp.domains)
	verify_dests(p.outgoing.udp.ips)
	verify_dests(p.outgoing.udp.domains)
	verify_dests(p.outgoing.other.ips)
	verify_dests(p.outgoing.other.domains)
	verify_dests(p.incoming.tcp.ips)
	verify_dests(p.incoming.udp.ips)
	verify_dests(p.incoming.other.ips)
end

function profile.from_json(s)
	local p = json.decode(s)

	profile.verify(p)

	return p
end

function profile.to_json(p)
	profile.verify(p)

	return json.encode(p)
end

return profile
