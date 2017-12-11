#!/usr/bin/env lua

local luamud = require 'luamud'

local acl, err = luamud.mud_create_from_file("tests/test.json")
if acl == nil then
    print("Error: " .. err)
end
print(acl:to_json())
