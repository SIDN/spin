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
local fw = require "fw"
local profile = require "profile"

-- https://github.com/flukso/lua-mosquitto
local json = require "json"
local mqtt = require "mosquitto"
local signal = require "posix.signal"

local FW_CHANNEL = "SPIN/fw"

function vprintf(msg)
	if verbose then
		print(msg)
	end
end

local client = mqtt.new()
client.ON_CONNECT = function()
	print("Connected to MQTT broker")
	client:subscribe(FW_CHANNEL)
	print("Subscribed to " .. FW_CHANNEL)
end

client.ON_DISCONNECT = function()
	print("DISCONNECTED")
	shutdown(0)
end

function handle_msg(p)
	fw.generate(p)

	os.exit(0) -- XXX
end

client.ON_MESSAGE = function(mid, topic, payload)
	if topic == FW_CHANNEL then
		msg = profile.from_json(payload)

		handle_msg(msg)
	else
		vprintf("message on UNKNOWN channel: " .. topic)
	end
end

function shutdown(signum)
	print("Done, exiting\n")
	os.exit(128+signum);
end

signal.signal(signal.SIGINT, shutdown)
signal.signal(signal.SIGKILL, shutdown)

broker = arg[1] -- defaults to "localhost" if arg not set
if (broker) then
	print("Connecting to " .. broker .. "...")
else
	print("Connecting to localhost...")
end
assert(client:connect(broker))

while true do
	client:loop(1)
end
