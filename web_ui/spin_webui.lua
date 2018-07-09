local mt_engine = require 'minittp_engine'
local mt_io = require 'minittp_io'
local mt_util = require 'minittp_util'

local copas = require 'copas'
local liluat = require 'liluat'

local mqtt = require 'mosquitto'
local json = require 'json'

local TRAFFIC_CHANNEL = "SPIN/traffic"
local HISTORY_SIZE = 600

local profile_manager_m = require 'profile_manager'

posix = require 'posix'

--
-- config file parser, reads config files
-- in the 'luci-style' form of
--
-- config <name>
--     option <name> '<value>'
--     option <name> '<value>'
-- config <name>
--     option <name> '<value>'
--
-- returns a straight list of all the whitespace-separated tokens
-- in the given filename
-- or nil, err on error
function file_tokenize(filename)
    local result = {}
    local fr, err = mt_io.file_reader(filename)
    if fr == nil then return nil, err end

    for line in fr:read_line_iterator(true) do
        for token in line:gmatch("%S+") do table.insert(result, token) end
    end

    return result
end

function file_tokenize_iterator(filename)
    local data, err = file_tokenize(filename)
    if data == nil then return nil, err end

    result = {}
    result.index = 1
    result.done = false
    result.data = data

    function result:nxt()
        if not self.done then
            local value = self.data[self.index]
            self.index = self.index + 1
            if self.index > table.getn(self.data) then self.done = true end
            return value
        else
            return nil
        end
    end
    return result
end

function strip_quotes(value)
    if value:len() == 1 then return value end
    if value:startswith("'") and value:endswith("'") then
        return value:sub(2, value:len()-1)
    elseif value:startswith('"') and value:endswith('"') then
        return value:sub(2, value:len()-1)
    else
        return value
    end
end

-- very basic config parser; hardly any checking
function config_parse(filename)
    local config = {}
    local tokens, err = file_tokenize_iterator(filename)
    if tokens == nil then return nil, err end

    local cur_section = "main"

    while not tokens.done do
        local token = tokens:nxt()
        if token == "config" then
            cur_section = tokens:nxt()
        elseif token == "option" then
            local option_name = tokens:nxt()
            local option_val = strip_quotes(tokens:nxt())
            if config[cur_section] == nil then config[cur_section] = {} end
            config[cur_section][option_name] = option_val
            print(config[cur_section][option_name])
        end
    end
    return config
end

--
-- minittp handler functionality
--
handler = {}

function handler:load_templates()
  self.templates = {}
  self.base_template_name = "base.html"
  local dirname = 'templates/'
  local p = mt_io.subprocess('ls', {dirname}, nil, true)
  for name in p:read_line_iterator(true) do
    if name:endswith('.html') then
      self.templates[name] = liluat.compile_file("templates/" .. name)
    end
  end
  p:close()
end

-- we have a two-layer template system; rather than adding headers
-- and footers to each template, we have a base template, which gets
-- the output from the 'inner' template as an argument ('main_html')
-- The outer template is called BASE
function handler:render(template_name, args)
  if not args then args = {} end
  if not self.templates[template_name] then
    return "[Error: could not find template " .. template_name .. "]"
  end
  --args['langkeys'] = language_keys
  args['main_html'] = liluat.render(self.templates[template_name], args)
  return liluat.render(self.templates['base.html'], args)
end

-- If you want to have a different base, or just render a section of
-- a page, use render_raw
function handler:render_raw(template_name, args)
  if not args then args = {} end
  if not self.templates[template_name] then
    return nil, "[Error: could not find template " .. template_name .. "]"
  end
  --args['langkeys'] = language_keys
  return liluat.render(self.templates[template_name], args)
end

function help(rcode, msg)
    if msg ~= nil then print(msg.."\n") end
    print("Usage: lua spin_webui.lua [options]")
    print("Options:")
    print("-c <configfile> Read settings from configfile")
    print("-m <mqtt_host>  Connect to MQTT at host (defaults to 127.0.0.1)")
    print("-p <mqtt_port>  Connect to MQTT at port (defaults to 1883)")
    print("-h              Show this help")
    os.exit(rcode)
end

function arg_parse(args)
    config_file = nil
    mqtt_host = nil
    mqtt_port = nil

    if args == nil then return config_file, mqtt_host, mqtt_port end

    skip = false
    for i = 1,table.getn(args) do
        if skip then
            skip = false
        elseif args[i] == "-h" then
            help()
        elseif args[i] == "-c" then
            config_file = args[i+1]
            if config_file == nil then help(1, "missing argument for -c") end
            skip = true
        elseif args[i] == "-m" then
            mqtt_host = args[i+1]
            if port == nil then help(1, "missing argument for -m") end
            skip = true
        elseif args[i] == "-p" then
            mqtt_port = tonumber(args[i+1])
            if port == nil then help(1, "missing or bad argument for -p") end
            skip = true
        else
            help(1, "Too many arguments at '" .. args[i] .. "'")
        end
    end

    return config_file, mqtt_host, mqtt_port
end

function handler:read_config(args)
    local config_file, mqtt_host, mqtt_port = arg_parse(args)
    local config = {}
    config['mqtt'] = {}
    config['mqtt']['host'] = "127.0.0.1"
    config['mqtt']['port'] = 1883
    if config_file ~= nil then
        config, err = config_parse(config_file)
        if config == nil then return nil, err end
    end
    if mqtt_host ~= nil then
        config['mqtt']['host'] = mqtt_host
    end
    if mqtt_port ~= nil then
        config['mqtt']['port'] = mqtt_port
    end
    self.config = config
end

function handler:add_device_seen(mac, name, timestamp)
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

        -- this device is new, so send a notification
        local notification_txt = "New device on network! Please set a profile"
        self:create_notification(notification_txt, mac, name)
    end
end

function handler:handle_traffic_message(data, orig_data)
    if data.flows == nil then return end
    for i, d in ipairs(data.flows) do
        local mac = nil
        local ips = nil
        local name = nil
        if d.from ~= nil and d.from.mac ~= nil then
            name = d.from.name
            mac = d.from.mac
            ips = d.from.ips
        elseif d.to ~= nil and d.to.mac ~= nil then
            name = d.to.name
            mac = d.to.mac
            ips = d.to.ips
        end

        if mac ~= nil then
            -- If we don't have a name yet, use an IP address
            if name == nil then
              if ips ~= nil and table.getn(ips) > 0 then
                  name = ips[1]
              -- to be sure we have *something* , fall back to mac
              else
                  d.name = mac
              end
            end
            -- gather additional info, if available
            self:add_device_seen(mac, name, os.time())
        end
    end
end

function handler:mqtt_looper()
    while true do
        self.client:loop()
        copas.sleep(0.1)
    end
end

function handler:handle_index(request, response)
    html, err = self:render("index.html", {mqtt_host = self.config['mqtt']['host']})
    if html == nil then
        response:set_status(500, "Internal Server Error")
        response.content = "Template error: " .. err
        return response
    end
    response.content = html
    return response
end

function get_tcpdump_pname(request, mac)
    return request.client_address .. "-" .. mac
end

--
-- Class for managing tcpdump processes
--
local tcpdumper = {}
tcpdumper.__index = tcpdumper

function tcpdumper.create(device, response)
    local td = {}
    setmetatable(td, tcpdumper)
    -- check device for format here or at caller?
    -- should be aa:bb:cc:dd:ee:ff
    td.running = true
    td.bytes_sent = 0
    td.response = response

    local subp, err = mt_io.subprocess("tcpdump", {"-U", "-i", "br-lan", "-s", "1600", "-w", "-", "ether", "host", device}, 0, true, false, false)
    if subp == nil then
        return nil
    end
    td.subp = subp

    return td
end

function tcpdumper:read_and_send(size)
    line, err = self.subp:read_bytes(size)
    if line == nil then
        if err ~= "read timed out" then
            print("Error reading from subprocess: " .. err)
            sent, err = response:send_chunk("")
            subp:kill()
            subp:close()
            return nil, err
        end
    else
        sent, err = self.response:send_chunk(line)
        if sent == nil then
            sent, err = self.response:send_chunk("")
            print("Error sending data: " .. err)
            subp:kill()
            subp:close()
            return nil, err
        else
            -- do not count the \r\n that signals end of chunk
            self.bytes_sent = self.bytes_sent + sent - 2
            return sent - 2
        end
    end
end

function tcpdumper:run()
    while self.running do
        self:read_and_send(1600)
        copas.sleep(0.1)
    end
    self.subp:kill()
    self.subp:close()

    -- End with an empty chunk, as per transfer-encoding: chunked protocol
    sent, err = self.response:send_chunk("")
    if sent == nil then
        print("Error sending data: " .. err)
    else
        print("Sent " .. " bytes");
    end
    -- just to make sure
    self.running = false
    return nil
end

function tcpdumper:stop()
    self.running = false
end

function handler:handle_tcpdump_start(request, response)
    local device = request.params["device"]
    local dname = get_tcpdump_pname(request, device)

    if self.active_dumps[dname] ~= nil then return nil, "already running" end
    local dumper, err = tcpdumper.create(device, response)
    -- todo: 500 internal server error?
    if dumper == nil then return nil, err end
    self.active_dumps[dname] = dumper

    response:set_header("Transfer-Encoding", "chunked")
    response:set_header("Content-Disposition",  "attachment; filename=\"tcpdump_"..device..".pcap\"")
    response:send_status()
    response:send_headers()

    dumper:run()
    -- remove it again
    self.active_dumps[dname] = nil
    return nil
end

function handler:handle_tcpdump_status(request, response)
    local device = request.params["device"]
    local dname = get_tcpdump_pname(request, device)
    local running = false
    local bytes_sent = 0
    if self.active_dumps[dname] ~= nil then
        running = true
        bytes_sent = self.active_dumps[dname].bytes_sent
    end

    html, err = self:render_raw("tcpdump_status.html", { device=device, running=running, bytes_sent=bytes_sent })

    if html == nil then
        response:set_status(500, "Internal Server Error")
        response.content = "Template error: " .. err
        return response
    end
    response.content = html
    return response
end

function handler:handle_tcpdump_stop(request, response)
    local device = request.params["device"]
    local dname = get_tcpdump_pname(request, device)

    if self.active_dumps[dname] ~= nil then
        self.active_dumps[dname]:stop()
    end
    response:set_header("Location", "/spin/tcpdump?device=" .. device)
    response:set_status(302, "Found")
    return response
end

function handler:handle_tcpdump_manage(request, response)
    local device = request.params["device"]
    local dname = get_tcpdump_pname(request, device)
    local running = false
    local bytes_sent = 0
    if self.active_dumps[dname] ~= nil then
        running = true
        bytes_sent = self.active_dumps[dname].bytes_sent
    end

    html, err = self:render_raw("tcpdump.html", { device=device, running=running, bytes_sent=bytes_sent })

    if html == nil then
        response:set_status(500, "Internal Server Error")
        response.content = "Template error: " .. err
        return response
    end
    response.content = html
    return response
end

function handler:set_cors_headers(response)
    response:set_header("Access-Control-Allow-Origin", "*")
    response:set_header("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept")
    response:set_header("Access-Control-Allow-Methods", "GET,POST,HEAD,OPTIONS")
end

function handler:set_api_headers(response)
    self:set_cors_headers(response)
    response:set_header("Content-Type", "application/json")
end

function handler:handle_device_list(request, response)
    self:set_api_headers(response)
    response.content = json.encode(self.devices_seen)
    return response
end

function handler:handle_profile_list(request, response)
    self:set_api_headers(response)
    local profile_list = {}
    for i,v in pairs(self.profile_manager.profiles) do
        table.insert(profile_list, v)
    end
    response.content = json.encode(profile_list)
    return response
end

function handler:handle_device_profiles(request, response, device_mac)
    self:set_api_headers(response)
    if request.method == "GET" then
        response.content = json.encode(self.profile_manager:get_device_profiles(device_mac))
    else
        if request.post_data ~= nil and request.post_data.profile_id ~= nil then
            local profile_id = request.post_data.profile_id
            local status = nil
            if self.devices_seen[device_mac] ~= nil then
                status, err = self.profile_manager:set_device_profile(device_mac, request.post_data.profile_id)
                local device_name = self.devices_seen[device_mac].name
                local profile_name = self.profile_manager.profiles[profile_id]
                if status then
                  local notification_txt = "Profile set to " .. profile_name)
                  self:create_notification(notification_txt, device_mac, device_name)
                else
                  local notification_txt = "Error setting device profile: " .. err
                  self:create_notification(notification_txt, device_mac, device_name)
                end
            else
                status = nil
                err = "Error: unknown device: " .. device_mac
            end
            -- persist the new state
            if status ~= nil then
                -- all ok, basic 200 response good
                self.profile_manager:save_device_profiles()
                return response
            else
                -- todo: how to convey error?
                response:set_status(400, "Bad request")
                response.content = json.encode({status = 400, error = err})
            end
        else
            response:set_status(400, "Bad request")
            response.content = json.encode({status = 400, error = "Parameter missing in POST data: profile_id"})
        end
    end
    return response
end

function handler:handle_toggle_new(request, response, device_mac)
    self:set_api_headers(response)
    if request.method == "POST" then
        if self.devices_seen[device_mac] ~= nil then
            self.devices_seen[device_mac].new = not self.devices_seen[device_mac].new
        else
            response:set_status(400, "Bad request")
            response.content = json.encode({status = 400, error = "Unknown device: " .. device_mac})
        end
    end
    return response
end

-- TODO: move to own module?
-- (down to TODO_MOVE_ENDS_HERE)
function handler:create_notification(text, device_mac, device_name)
    local new_notification = {}
    new_notification['id'] = self.notification_counter
    self.notification_counter = self.notification_counter + 1
    new_notification['timestamp'] = os.time()
    new_notification['message'] = text
    if device_mac ~= nil then
        new_notification['deviceMac'] = device_mac
    end
    if device_name ~= nil then
        new_notification['deviceName'] = device_name
    end
    table.insert(self.notifications, new_notification)
end

function handler:delete_notification(id)
    local to_remove = nil
    for i,v in pairs(self.notifications) do
        if v.id == id then
            to_remove = i
        end
    end
    if to_remove ~= nil then table.remove(self.notifications, to_remove) end
end

function handler:handle_notification_list(request, response)
    self:set_api_headers(response)
    response.content = json.encode(self.notifications)
    return response
end

function handler:handle_notification_delete(request, response, notification_id)
    self:set_api_headers(response)
    if request.method == "POST" then
        self:delete_notification(tonumber(notification_id))
    else
        response.set_status(400, "Bad request")
        response.content = "This URL requires a POST"
    end
    return response
end

function handler:handle_notification_add(request, response)
    if request.method == "POST" then
        if request.post_data ~= nil and request.post_data.message ~= nil then
            self:create_notification(request.post_data.message)
            return response
        else
            response:set_status(400, "Bad request")
            response.content = "No element 'message' found in post data"
        end
    else
        response.set_status(400, "Bad request")
        response.content = "This URL requires a POST"
    end
    return response
end

-- TODO_MOVE_ENDS_HERE

function handler:init(args)
    -- we keep track of active downloads by having a dict of
    -- "<client_ip>-<device mac>" -> <bytes_sent>
    self.active_dumps = {}

    self:read_config(args)
    self:load_templates()
    
    self.profile_manager = profile_manager_m.create_profile_manager()
    self.profile_manager:load_all_profiles()
    self.profile_manager:load_device_profiles()
    self.notifications = {}
    self.notification_counter = 1

    -- We will use this list for the fixed url mappings
    -- Fixed handlers are interpreted as they are; they are
    -- ONLY valid for the EXACT path identified in this list
    self.fixed_handlers = {
        ["/"] = handler.handle_index,
        ["/spin_api"] = self.handle_index,
        ["/spin_api/"] = self.handle_index,
        ["/spin_api/tcpdump"] = self.handle_tcpdump_manage,
        ["/spin_api/tcpdump_status"] = self.handle_tcpdump_status,
        ["/spin_api/tcpdump_start"] = self.handle_tcpdump_start,
        ["/spin_api/tcpdump_stop"] = self.handle_tcpdump_stop,
        ["/spin_api/devices"] = self.handle_device_list,
        ["/spin_api/profiles"] = self.handle_profile_list,
        ["/spin_api/notifications"] = self.handle_notification_list,
        ["/spin_api/notifications/create"] = self.handle_notification_add
    }

    -- Pattern handlers are more flexible than fixed handlers;
    -- they use lua patterns to find a handler
    -- Take care; it is first-come-first-serve in this list
    -- Capture values are passed to the handler as extra arguments
    -- The maximum number of capture fields is 4
    self.pattern_handlers = {
        { pattern = "/spin_api/devices/(%x%x:%x%x:%x%x:%x%x:%x%x:%x%x)/appliedProfiles/?$",
          handler = self.handle_device_profiles
        },
        { pattern = "/spin_api/devices/(%x%x:%x%x:%x%x:%x%x:%x%x:%x%x)/toggleNew/?$",
          handler = self.handle_toggle_new
        },
        { pattern = "/spin_api/notifications/(%d+)/delete/?$",
          handler = self.handle_notification_delete
        }
    }

    local client = mqtt.new()
    self.devices_seen = {}

    client.ON_CONNECT = function()
        vprint("Connected to MQTT broker")
        client:subscribe(TRAFFIC_CHANNEL)
        vprint("Subscribed to " .. TRAFFIC_CHANNEL)
        if handle_incidents then
            client:subscribe(INCIDENT_CHANNEL)
            vprint("Subscribed to " .. INCIDENT_CHANNEL)
        end
    end

    local h = self

    client.ON_MESSAGE = function(mid, topic, payload)
        --print("[XX] message for you, sir!")
        local success, pd = pcall(json.decode, payload)
        if success and pd then
            if topic == TRAFFIC_CHANNEL then
                if pd["command"] and pd["command"] == "traffic" then
                    h:handle_traffic_message(pd["result"], payload)
                end
            elseif handle_incidents and topic == INCIDENT_CHANNEL then
                if pd["incident"] == nil then
                    print("Error: no incident data found in " .. payload)
                    print("Incident report ignored")
                else
                    local incident = pd["incident"]
                    local ts = incident["incident_timestamp"]
                    for i=ts-5,ts+5 do
                        if handle_incident_report(incident, i) then break end
                    end
                end
            end
        end
    end

    print("[XX] connecting to " .. self.config.mqtt.host .. ":" .. self.config.mqtt.port)
    a,b,c,d = client:connect(self.config.mqtt.host, self.config.mqtt.port)
    --print(client:socket)
    --client.socket = copas.wrap(client.socket)
    --if a ~= nil then print("a: " .. a) end
    if b ~= nil then print("b: " .. b) end
    if c ~= nil then print("c: " .. c) end
    if d ~= nil then print("d: " .. d) end

    self.client = client
    copas.addthread(self.mqtt_looper, self)

    return true
end

function handler:handle_request(request, response)
    local result = nil
    -- handle OPTIONS separately for now
    if request.method == "OPTIONS" then
        self:set_cors_headers(response)
        return response
    end

    local handler = self.fixed_handlers[request.path]
    if handler ~= nil then
        return handler(self, request, response)
    else
        -- see if it matches one of the pattern handlers
        for i, ph in pairs(self.pattern_handlers) do
            local s,e,arg1,arg2,arg3,arg4 = string.find(request.path, ph.pattern)
            if s ~= nil and e ~= nil then
                return ph.handler(self, request, response, arg1, arg2, arg3, arg4)
            end
        end
        -- try one of the static files; note: relative path
        return mt_engine.handle_static_file(request, response, "static")
    end
    return response
end

return handler
