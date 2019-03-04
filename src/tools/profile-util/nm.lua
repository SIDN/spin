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

local json = require "json"
-- http://lua.sqlite.org/index.cgi/doc/tip/doc/lsqlite3.wiki
local sqlite3 = require "lsqlite3"

local nm = {}

local WRITE_DB_EVERY_SECS = 300

local FLOW_TIMEOUT = 3600

local DB_SCHEMA_FILE = "db.schema"

local db

local state = nil
local function state_new()
	return {
	    initialized = false,
	    sql_last_commit = nil,
	    stmt_dns_insert = nil,
	    stmt_flow_insert = nil,
	    stmt_flow_select = nil,
	    stmt_flow_update = nil,
	    verbose = false,
	}
end

function vprintf(msg)
	if state.verbose then
		print(msg)
	end
end

-- XXX: maybe limit 1 is not necessary?
local QUERY_FLOW_SELECT = "select id, packets, bytes from flows "
    .. "where ip_from = $1 and ip_to = $2 and ip_proto = $3 "
    .. "and port_from = $4 and port_to = $5 and last_timestamp > $6 "
    .. "order by id desc limit 1"
local QUERY_FLOW_UPDATE = "update flows set packets = $1, bytes = $2, "
    .. "last_timestamp = $3 where id = $4"
local QUERY_FLOW_INSERT = "insert into flows (mac_from, mac_to, ip_from, "
    .. "ip_to, ip_proto, tcp_initiated, port_from, "
    .. "port_to, packets, bytes, first_timestamp, "
    .. "last_timestamp) "
    .. "values ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, "
    .. "$12)"
-- XXX: MAC address only gets inserted, not updated; this means that if the MAC
-- address is not known to be local at insertion time, MAC address won't show
-- up at all.
local function update_insert_flow(mac_from, mac_to, ip_from, ip_to, ip_proto,
    tcp_initiated, port_from, port_to, packets, bytes, timestamp)
	assert(ip_from)
	assert(ip_to)
	assert(ip_proto)
	assert(port_from)
	assert(port_to)
	assert(packets)
	assert(bytes)
	assert(timestamp)

	local before = timestamp - FLOW_TIMEOUT
	local err = state.stmt_flow_select:bind_values(ip_from, ip_to, ip_proto,
	    port_from, port_to, before)
	assert(err, db:errmsg())

	local id, old_packets, old_bytes
	local i = 0
	for _id, _packets, _bytes in state.stmt_flow_select:urows() do
		assert(_id)
		assert(_packets)
		assert(_bytes)
		id = _id
		old_packets = _packets
		old_bytes = _bytes
		i = i + 1
	end
	assert(i == 0 or i == 1,
	    string.format("not the correct number of rows: %d", i))
	state.stmt_flow_select:reset()

	if (id) then
		vprintf("U")
		local err = state.stmt_flow_update:bind_values(
		    (old_packets + packets), (old_bytes + bytes), timestamp, id)
		assert(err, db:errmsg())
		local err = state.stmt_flow_update:step()
		assert(err == sqlite3.DONE, db:errmsg())
		state.stmt_flow_update:reset()
	else
		vprintf("I")
		local err = state.stmt_flow_insert:bind_values(mac_from, mac_to,
		    ip_from, ip_to, ip_proto, tcp_initiated, port_from, port_to,
		    packets, bytes, timestamp, timestamp)
		assert(err, db:errmsg())
		local err = state.stmt_flow_insert:step()
		assert(err == sqlite3.DONE, db:errmsg())
		state.stmt_flow_insert:reset()
	end
end

local function handle_traffic(msg)
	assert(msg.command == "traffic")
	assert(msg.result.flows)
	assert(msg.result.timestamp)

	for _,flow in pairs(msg.result.flows) do
		assert(flow.from)
		assert(flow.to)

		mac_from = ""
		if (flow.from.mac) then
			mac_from = flow.from.mac
		end
		mac_to = ""
		if (flow.to.mac) then
			mac_to = flow.to.mac
		end
		ip_from = ""
		if (flow.from.ips[1]) then
			-- XXX
			ip_from = flow.from.ips[1]
		end
		ip_to = ""
		if (flow.to.ips[1]) then
			-- XXX
			ip_to = flow.to.ips[1]
		end
		ip_proto = -1
		if (flow.protocol) then
			ip_proto = flow.protocol
		end
		tcp_initiated = 0
		if (flow.x_tcp_initiated) then
			tcp_initiated = flow.x_tcp_initiated
		end

		update_insert_flow(mac_from, mac_to, ip_from, ip_to, ip_proto,
		    tcp_initiated, flow.from_port, flow.to_port, flow.count,
		    flow.size, msg.result.timestamp)
	end
end

local QUERY_DNS_INSERT = "insert into dns (domain, ip, timestamp) "
    .. "values ($1, $2, $3)"
local function handle_dnsquery(msg)
	assert(msg.command == "dnsquery")
	assert(msg.result)
	assert(msg.result.queriednode)
	assert(msg.result.queriednode.ips)

	local query = msg.result.query
	if (#msg.result.queriednode.ips > 0) then
		local str = "DNS answer: " .. query .. ": "
		for _,ip in pairs(msg.result.queriednode.ips) do
			str = str .. ip .. " "
			local err = state.stmt_dns_insert:bind_values(query, ip,
			    msg.result.queriednode.lastseen)
			assert(err, db:errmsg())
			local err = state.stmt_dns_insert:step()
			assert(err == sqlite3.DONE, db:errmsg())
			state.stmt_dns_insert:reset()
		end
		vprintf(str)
	else
		vprintf("DNS query: " .. query)
	end
end

function nm.handle_msg(payload)
	assert(state.initialized, "nm not initialized")

	msg = json.decode(payload)

	assert(msg.command)

	if (msg.command == "traffic") then
		handle_traffic(msg)
	elseif (msg.command == "dnsquery") then
		handle_dnsquery(msg)
	else
		vprintf("message with UNHANDLED command: " ..
		    msg.command .. ", message: " .. payload)
	end
end

function nm.initialize(verbose, db_file)
	assert(state == nil or not state.initialized, "nm already initialized")

	state = state_new()

	state.verbose = verbose

	db = sqlite3.open(db_file)
	assert(db)
	assert(db:exec("PRAGMA foreign_keys = ON;"), db:errmsg())
	-- XXX Not sure which journal_mode we should pick. On one hand, we want
	-- XXX to reduce disk I/O (on the Valibox anyway) but it is possible to
	-- XXX corrupt the database which is not very nice.
	-- XXX See https://www.sqlite.org/pragma.html#pragma_journal_mode
	assert(db:exec("PRAGMA journal_mode = OFF;"), db:errmsg())
	-- XXX Look into https://www.sqlite.org/pragma.html#pragma_synchronous
	assert(db:exec("begin"), db:errmsg())

	-- Verify whether database has been set up
	local stmt = db:prepare("select id from flows")
	if not stmt then
		vprintf("database appears to be empty or does not exist, "
		    .. "creating")

		local f = assert(io.open(DB_SCHEMA_FILE, "r"))
		local s = f:read("*a")
		assert(s, "did not expect EOF yet")
		f:close()

		assert(db:exec(s), db:errmsg())
	else
		stmt:finalize()
	end

	state.sql_last_commit = os.time()

	-- Initialize prepared statements
	state.stmt_dns_insert = db:prepare(QUERY_DNS_INSERT)
	assert(state.stmt_dns_insert, db:errmsg())
	state.stmt_flow_insert = db:prepare(QUERY_FLOW_INSERT)
	assert(state.stmt_flow_insert, db:errmsg())
	state.stmt_flow_select = db:prepare(QUERY_FLOW_SELECT)
	assert(state.stmt_flow_select, db:errmsg())
	state.stmt_flow_update = db:prepare(QUERY_FLOW_UPDATE)
	assert(state.stmt_flow_update, db:errmsg())

	state.initialized = true
end

function nm.shutdown()
	assert(state.initialized, "nm not initialized")

	-- Finalize prepared statements
	assert(state.stmt_dns_insert:finalize() == sqlite3.OK, db:errmsg())
	assert(state.stmt_flow_insert:finalize() == sqlite3.OK, db:errmsg())
	assert(state.stmt_flow_select:finalize() == sqlite3.OK, db:errmsg())
	assert(state.stmt_flow_update:finalize() == sqlite3.OK, db:errmsg())

	db:exec("commit")

	db = nil

	state.sql_last_commit = nil

	state.initialized = false

	state = nil
end

function nm.periodic_store()
	assert(state.initialized, "nm not initialized")

	if (state.sql_last_commit < os.time() - WRITE_DB_EVERY_SECS) then
		vprintf("COMMIT")
		assert(db:exec("commit"), db:errmsg())
		state.sql_last_commit = os.time()
		assert(db:exec("begin"), db:errmsg())
	end	
end

return nm
