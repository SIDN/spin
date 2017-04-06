#!/usr/bin/lua

local lnflog = require 'lnflog'

--print(lnflog.sin(lnflog.pi))

function my_cb(mydata, event)
    print("Event:")
    print("  from: " .. event:get_from_addr())
    print("  to:   " .. event:get_to_addr())
    print("  size: " .. event:get_payload_size())
end

local mydata = {}
mydata.foo = 123
mydata.bar = "asdf"

nl = lnflog.setup_netlogger_loop(1, my_cb, mydata)
for i=1,100 do
    nl:loop_once()
end
nl:close()
