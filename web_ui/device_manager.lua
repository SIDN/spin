
--
-- This class keeps track of the devices in the local network
--

local spin_util = require('spin_util')

local _M = {}

local device_manager = {}
device_manager_mt = { __index = device_manager }

function _M.create(profile_manager)
  local newinst = {}
  setmetatable(newinst, device_manager_mt)

  newinst.devices_seen = {}
  newinst.profile_manager = profile_manager
  newinst.last_update = spin_util.get_time_string()

  return newinst
end

function device_manager:get_device_seen(mac)
    return self.devices_seen[mac]
end

function device_manager:get_devices_seen()
    return self.devices_seen
end

function device_manager:device_is_new(mac)
    return (self.devices_seen[mac] ~= nil) and (self.devices_seen[mac].new)
end

function device_manager:set_device_is_new(mac, value)
    self.devices_seen_updated = spin_util.get_time_string()
    self.devices_seen[mac] = value
end


function device_manager:add_device_seen(mac, name, timestamp)
    local device_is_new = False
    if self.devices_seen[mac] ~= nil then
        self.devices_seen[mac]['lastSeen'] = timestamp
        self.devices_seen[mac]['name'] = name
        self.devices_seen[mac]['appliedProfiles'] = self.profile_manager:get_device_profiles(mac)
    else
        local device_data = {}
        device_data['lastSeen'] = timestamp
        device_data['name'] = name
        device_data['new'] = true
        device_data['mac'] = mac
        device_data['appliedProfiles'] = self.profile_manager:get_device_profiles(mac)
        device_data['enforcement'] = ""
        device_data['logging'] = ""

        self.devices_seen[mac] = device_data

        device_is_new = True
    end
    self.devices_seen_updated = spin_util.get_time_string()
    return device_is_new
end


return _M
