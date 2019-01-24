#!/usr/bin/env lua

local luamud = require 'luamud'

local mud, err = luamud.mud_create_from_file("tests/test.json")
if mud == nil then
    print("Error: " .. err)
else
    print(mud:to_json())
    print("MUD URL: " .. mud:get_mud_url())
    print("Last update: " .. mud:get_last_update())
    print("Cache validity: " .. mud:get_cache_validity())
    if mud:get_is_supported() then
        print("Supported: Yes")
    else
        print("Supported: No")
    end
    if mud:get_systeminfo() then
        print("Systeminfo: " .. mud:get_systeminfo())
    else
        print("Systeminfo not set")
    end

    print("Globally defined acls:")
    for _,acl in pairs(mud:get_acls()) do
        print("    " .. acl:get_name())
        for _,r in pairs(acl:get_rules()) do
            print("        " .. r:get_name())
        end
    end

    print("From-device policy:")
    for acl_n, acl_type in pairs(mud:get_from_device_acls()) do
        print("    " .. acl_n .. " (" .. acl_type .. ")")
    end

    print("To-device policy:")
    for acl_n, acl_type in pairs(mud:get_to_device_acls()) do
        print("    " .. acl_n .. " (" .. acl_type .. ")")
    end
end
