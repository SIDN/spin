local mt_io = require 'minittp_io'
local mt_util = require 'minittp_util'

local posix_dirent = require 'posix.dirent'
local json = require 'json'

local _M = {}

local PROFILE_DIRECTORY = "/etc/spin/profiles"
local DEVICE_PROFILE_FILE = "/etc/spin/device_profiles.conf"

local profile_manager = {}
profile_manager.__index = profile_manager

function _M.create_profile_manager()
    local pm = {}
    setmetatable(pm, profile_manager)
    pm.profile_directory = PROFILE_DIRECTORY
    pm.device_profile_file = DEVICE_PROFILE_FILE
    pm.profiles = {}
    pm.device_profiles = {}
    return pm
end

function profile_manager:load_all_profiles()
    self.profiles = {}
    -- for now, we simply load the profiles from a local directory
    for f in posix_dirent.files(self.profile_directory) do
        if f:endswith(".json") then
            local fr, err = mt_io.file_reader(self.profile_directory .. "/" .. f)
            if fr ~= nil then
                local profile = json.decode(fr:read_lines_single_str())
                if profile.id ~= nil then
                    self.profiles[profile.id] = profile
                else
                    print("[XX] error: profile has no ID, not a profile? " .. f)
                end
            else
                print("[XX] error: " .. err)
            end
        end
    end
end

function profile_manager:save_device_profiles()
    local fw, err = mt_io.file_writer(self.device_profile_file)
    if fw ~= nil then
        for device, profiles in pairs(self.device_profiles) do
            local line = device .. " " .. table.concat(profiles, " ")
            print("[XX] STORE LINE: '" .. line .. "'")
            fw:write_line(line, true)
        end
        fw:close()
    end
end

function profile_manager:get_device_profiles(device_mac)
    if self.device_profiles[device_mac] ~= nil then
        return self.device_profiles[device_mac]
    else
        print(json.encode(self.device_profiles))
        return {}
    end
end

-- note: at this time, we can only *set* one at a time (but we store
-- them as if we can set multiple)
function profile_manager:set_device_profile(device_mac, profile_id)
    if self.profiles[profile_id] ~= nil then
        self.device_profiles[device_mac] = { profile_id }
    else
        return nil, "Error: unknown profile: " .. profile_id
    end
    return true
end

function profile_manager:load_device_profiles()
    self.device_profiles = {}
    local result = {}
    print("[XX] dpf: " .. self.device_profile_file)
    local fr, err = mt_io.file_reader(self.device_profile_file)
    if fr == nil then return nil, err end

    for line in fr:read_line_iterator(true) do
        local tokens = {}
        for token in line:gmatch("%S+") do table.insert(tokens, token) end
        if table.getn(tokens) >= 2 then
            local device = table.remove(tokens, 1)
            self.device_profiles[device] = tokens
        end
    end

    return result
end

return _M
