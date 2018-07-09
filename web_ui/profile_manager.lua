local mt_io = require 'minittp_io'
local mt_util = require 'minittp_util'

local posix_dirent = require 'posix.dirent'
local json = require 'json'

local _M = {}

local PROFILE_DIRECTORY = "/etc/spin/profiles"
local DEVICE_PROFILE_FILE = "/etc/spin/device_profiles.conf"

local profile_manager = {}
profile_manager.__index = profile_manager

-- TODO: move this to mt_io
function shlex_split(str)
  local e = 0
  local cmd = nil
  local result = {}
  while true do
      local b = e+1
      b = str:find("%S",b)
      if b==nil then break end
      if str:sub(b,b)=="'" then
          e = str:find("'",b+1)
          b = b+1
      elseif str:sub(b,b)=='"' then
          e = str:find('"',b+1)
          b = b+1
      else
          e = str:find("%s",b+1)
      end
      if e==nil then e=#str+1 end
      if cmd == nil then
        cmd = str:sub(b,e-1)
      else
        table.insert(result, str:sub(b,e-1))
      end
  end
  return cmd, result
end

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
                local success, profile = pcall(json.decode, fr:read_lines_single_str())
                if success and profile then
                  if profile.id ~= nil then
                      self.profiles[profile.id] = profile
                  else
                      print("[XX] error: profile has no ID, not a profile? " .. f)
                  end
                else
                  print("[XX] error reading profile from " .. f .. ": " .. profile)
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
        result = {}
        table.insert(result, "allow_all")
        return result
    end
end

-- note: at this time, we can only *set* one at a time (but we store
-- them as if we can set multiple)
function profile_manager:set_device_profile(device_mac, profile_id)
    if self.profiles[profile_id] ~= nil then
        self.device_profiles[device_mac] = { profile_id }

        self:remove_device_profile(device_mac)
        if self.profiles[profile_id]["rules_v4"] ~= nil then
          self:apply_device_profile_v4(device_mac, self.profiles[profile_id]["rules_v4"])
        else
          print("[XX] no IPv4 rules defined in profile")
        end
        if self.profiles[profile_id]["rules_v6"] ~= nil then
          self:apply_device_profile_v6(device_mac, self.profiles[profile_id]["rules_v6"])
        else
          print("[XX] no IPv4 rules defined in profile")
        end
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
        print("[XX] line: '" .. line .. "'")
        for token in line:gmatch("%S+") do table.insert(tokens, token) end
        if table.getn(tokens) >= 2 then
            local device = table.remove(tokens, 1)
            -- apply it immediately
            self:remove_device_profile(device)
            for _,t in pairs(tokens) do
                local profile = self.profiles[t]
                if profile and profile.rules_v4 then
                    self:apply_device_profile_v4(device, profile.rules_v4)
                else
                    print("[XX] no profile or no rules: " .. t)
                end
                if profile and profile.rules_v6 then
                    self:apply_device_profile_v6(device, profile.rules_v6)
                else
                    print("[XX] no profile or no rules: " .. t)
                end
            end
            self.device_profiles[device] = tokens
        end
    end

    return result
end

function profile_manager:apply_device_profile_v4(device_mac, rules)
    -- the list of rules is interpreted in order,
    -- and the following prefix is user:
    -- iptables -I FORWARD -m mac --mac-source <mac> -m comment --comment "SPIN-<mac>"
    local base_rule = "iptables -I FORWARD -m mac --mac-source " .. device_mac .. " -m comment --comment \"SPIN-" .. device_mac .. "\" "
    print("[XX] apply device profile: ")
    print(table.getn(rules))
    -- hmmz, this is not atomic...
    for i,r in pairs(rules) do
        local cmd, args = shlex_split(base_rule .. r)
        local s, err = mt_io.subprocess(cmd, args)
        if s == nil then print("[XX] error starting subp: " .. cmd .. ": " .. err) end
        s:close()
    end
    print("[XX] device profile rules applied (IPv6)")
end

function profile_manager:apply_device_profile_v6(device_mac, rules)
    -- the list of rules is interpreted in order,
    -- and the following prefix is user:
    -- iptables -I FORWARD -m mac --mac-source <mac> -m comment --comment "SPIN-<mac>"
    local base_rule = "ip6tables -I FORWARD -m mac --mac-source " .. device_mac .. " -m comment --comment \"SPIN-" .. device_mac .. "\" "
    print("[XX] apply device profile: ")
    print(table.getn(rules))
    -- hmmz, this is not atomic...
    for i,r in pairs(rules) do
        local cmd, args = shlex_split(base_rule .. r)
        local s, err = mt_io.subprocess(cmd, args)
        if s == nil then print("[XX] error starting subp: " .. cmd .. ": " .. err) end
        s:close()
    end
    print("[XX] device profile rules applied (IPv6)")
end

function profile_manager:remove_device_profile_helper(device_mac, save_command, restore_command)
    -- remove any rules that have the mac address in them;
    -- since in theory the profile may have changed (or this
    -- software restarted), we don't delete them based on the
    -- profile, but purely on mac address and comment
    local to_remove = "SPIN%-" .. device_mac
    local removal_count = 0

    local s, err = mt_io.subprocess(save_command, {}, 0, true)
    if s == nil then
        return nil, err
    end
    local lines = s:read_lines(false, 5000)
    s:close()
    print("[XX] GOT " .. table.getn(lines) .. " original lines")
    local newlines = {}
    for i,l in pairs(lines) do
        if not l:find(to_remove) then
            table.insert(newlines, l)
        else
            removal_count = removal_count + 1
        end
    end

    -- small sanity check; if something went wrong and we have
    -- no rules left, do not reload the firewall rules
    print("[XX] Becomes " .. table.getn(lines) .. " new lines")
    if table.getn(newlines) > 0 then
        local sr, err = mt_io.subprocess(restore_command, {}, 0, false, false, true)
        if sr == nil then
            return nil, err
        else
            for i,l in pairs(newlines) do
            sr:write_line(l)
        end
        sr:close()
      end
    end

    return removal_count
end

function profile_manager:remove_device_profile(device_mac)
    return self:remove_device_profile_helper(device_mac, "iptables-save", "iptables-restore") +
           self:remove_device_profile_helper(device_mac, "ip6tables-save", "ip6tables-restore")
end

return _M
