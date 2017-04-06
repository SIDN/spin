#!/usr/bin/lua

local lnflog = require 'lnflog'

--print(lnflog.sin(lnflog.pi))

function my_cb(a,b,c)
  print("XXXXXXXXXXXX callback")
  print("XXXXXXXXXXXX a: " .. a)
  print("XXXXXXXXXXXX b: " .. b)
  print("XXXXXXXXXXXX c: " .. c)
end

function noarg_cb()
  print("callback no args")
end

local mydata = {}
mydata.foo = 123
mydata.bar = "asdf"

nl = lnflog.setup_netlogger_loop(1, my_cb, mydata)
--lnflog.loop_once(nl)
--lnflog.loop_once(nl)
for i=1,1000 do
    lnflog.loop_once(nl)
end
lnflog.close_netlogger(nl)

--lnflog.setup_netlogger_loop(1, noarg_cb, mydata)
