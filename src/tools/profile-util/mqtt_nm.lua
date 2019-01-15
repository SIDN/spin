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

local nm = require "nm"

-- https://github.com/flukso/lua-mosquitto
local mqtt = require "mosquitto"
local signal = require "posix.signal"

local TRAFFIC_CHANNEL = "SPIN/traffic"

local verbose = true

function vprintf(msg)
	if verbose then
		print(msg)
	end
end

local client = mqtt.new()
client.ON_CONNECT = function()
	print("Connected to MQTT broker")
	client:subscribe(TRAFFIC_CHANNEL)
	print("Subscribed to " .. TRAFFIC_CHANNEL)
end

client.ON_DISCONNECT = function()
	print("DISCONNECTED")
	shutdown(0)
end

client.ON_MESSAGE = function(mid, topic, payload)
	if topic == TRAFFIC_CHANNEL then
		nm.handle_msg(payload)
	else
		vprintf("message on UNKNOWN channel: " .. topic)
	end
end

function shutdown(signum)
	print("Done, exiting\n")
	nm.shutdown()
	os.exit(128+signum)
end

if not arg[1] then
	io.stderr:write[[
Usage: ./mqtt_nm.lua db_file [mqtt_host]
]]
	os.exit(1)
end

signal.signal(signal.SIGINT, shutdown)
signal.signal(signal.SIGKILL, shutdown)

broker = arg[2] -- defaults to "localhost" if arg not set
if (broker) then
	print("Connecting to " .. broker .. "...")
else
	print("Connecting to localhost...")
end
assert(client:connect(broker))

nm.initialize(verbose, arg[1])

while true do
	client:loop(1)
	nm.periodic_store()
end

