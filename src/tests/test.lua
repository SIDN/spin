#!/usr/bin/lua

local lnflog = require 'lnflog'

--print(lnflog.sin(lnflog.pi))

function my_cb(a,b,c)
  print("callback")
  print("a: " .. a)
  print("b: " .. b)
  print("c: " .. c)
end

function noarg_cb()
  print("callback no args")
end

local mydata = {}
mydata.foo = 123
mydata.bar = "asdf"

--lnflog.setup_netlogger_loop(1, my_cb, mydata)
lnflog.setup_netlogger_loop(1, noarg_cb, mydata)
