#!/usr/bin/env lua

local luamud = require 'luamud'

local mud, err = luamud.mud_create_from_file("tests/test.json")
if mud == nil then
    print("Error: " .. err)
end
print(mud:to_json())
print("MUD URL: " .. mud:get_mud_url())
print("Last update: " .. mud:get_last_update())
print("Cache validity: " .. mud:get_cache_validity())
if mud:get_is_supported() then
    print("Supported: Yes")
else
    print("Supported: No")
end

