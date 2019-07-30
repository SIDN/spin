--
-- This is the main spin web UI handling code
--
-- This class handles the incoming HTTP and websocket requests and
-- responses.
--
-- Internal SPIN functionality is generally handled by the _manager
-- classes

local coxpcall = require 'coxpcall'
local mt_engine = require 'minittp_engine'
local mt_io = require 'minittp_io'
local mt_util = require 'minittp_util'

local spin_util = require 'spin_util'

local copas = require 'copas'
local liluat = require 'liluat'

local mqtt = require 'mosquitto'
local json = require 'json'

-- Additional supporting tools
local ws_ext = require 'ws_ext'
local tcpdumper = require 'tcpdumper'

local TRAFFIC_CHANNEL = "SPIN/traffic"
local HISTORY_SIZE = 600

-- The managers implement the main functionality
local device_manager_m = require 'device_manager'
local profile_manager_m = require 'profile_manager'

local TEMPLATE_PATH = "templates/"

-- Some functionality requires RPC calls to the spin daemon.
-- This module automatically uses UBUS if available, and JSON-RPC
-- otherwise
local rpc = require('json_rpc')

posix = require 'posix'

--
-- minittp handler functionality
--
handler = {}

--
-- Loader for the templates in case of HTML responses
--
function handler:load_templates()
  self.templates = {}
  self.base_template_name = "base.html"
  local dirname = 'templates/'
  local p = mt_io.subprocess('ls', {dirname}, nil, true)
  for name in p:read_line_iterator(true) do
    if name:endswith('.html') then
      self.templates[name] = liluat.compile_file(TEMPLATE_PATH .. name)
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
        config, err = spin_util.config_parse(config_file)
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
    -- add_device_seen returns True if the device is 'new' (i.e. wasn't seen
    -- before)
    if (self.device_manager:add_device_seen(mac, name, timestamp)) then
        local notification_txt = "New device on network! Please set a profile"
        self:create_notification("new_device", {}, notification_txt, mac, name)
        self:send_websocket_update("newDevice", self.device_manager:get_device_seen(mac))
    else
        self:send_websocket_update("deviceUpdate", self.device_manager:get_device_seen(mac))
    end
end

-- TODO: should the device manager do this part too?
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

function handler:handle_mqtt_queue_msg(msg)
    -- TODO: pass topic? (traffic/incident)
    local success, pd = coxpcall.pcall(json.decode, msg)
    if success and pd then
        --print("[XX] msg: " .. msg)
        if pd["command"] and pd["command"] == "traffic" then
            self:handle_traffic_message(pd["result"], payload)
        elseif pd["incident"] ~= nil then
            local incident = pd["incident"]
            local ts = incident["incident_timestamp"]
            for i=ts-5,ts+5 do
                if self:handle_incident_report(incident, i) then break end
            end
        end
    end
end

function handler:mqtt_queue_looper()
    self.mqtt_queue_msgs = {}
    while true do
        while table.getn(self.mqtt_queue_msgs) > 0 do
          local msg = table.remove(self.mqtt_queue_msgs, 1)
          self:handle_mqtt_queue_msg(msg)
        end
        copas.sleep(0.1)
        for i,c in pairs(self.websocket_clients) do
          if c:has_queued_messages() then
            c:send_queued_messages()
            print("[XX] still queueud msgs")
          end
        end
    end
end

function handler:handle_index(request, response)
    html, err = self:render("index.html", {mqtt_host = self.config['mqtt']['host']})
    response:set_header("Last-Modified", spin_util.get_file_timestamp(TEMPLATE_PATH .. "index.html"))
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
    response:set_header("Last-Modified", spin_util.get_file_timestamp(TEMPLATE_PATH .. "tcpdump.html"))

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

function handler:handle_configuration(request, response)
    self:set_api_headers(response)
    if request.method == "GET" then
        -- read the config, we don't use it ourselves just yet
        local fr, err = mt_io.file_reader("/etc/spin/spin_webui.conf")
        response:set_header("Last-Modified", spin_util.get_file_timestamp("/etc/spin/spin_webui.conf"))
        if fr ~= nil then
          response.content = fr:read_lines_single_str()
        else
          if err ~= nil then print("[XX] error: " .. err) end
          -- we should put the error in here if it is not filenotfound
          response.content = "{}"
        end
    elseif request.method == "POST" then
        if request.post_data and request.post_data.config then
            local fw, err = mt_io.file_writer("/etc/spin/spin_webui.conf")
            if fw ~= nil then
                fw:write_line(request.post_data.config)
                fw:close()
            else
                response:set_status(400, "Bad request")
                response.content = json.encode({status = 400, error = err})
            end
        else
            response:set_status(400, "Bad request")
            response.content = json.encode({status = 400, error = "Missing value: 'config' in POST data"})
        end
    else
        response:set_status(405, "Method not allowed")
    end
    return response
end

function dump(o)
   if type(o) == 'table' then
      local s = '{ '
      for k,v in pairs(o) do
         if type(k) ~= 'number' then k = '"'..k..'"' end
         s = s .. '['..k..'] = ' .. dump(v) .. ','
      end
      return s .. '} '
   else
      return tostring(o)
   end
end

function handler:handle_device_list(request, response)
    print("Calling RPC")
    local conn = rpc.connect()
    if not conn then
        error("failed to connect to RPC mechanism")
    end
    result, err = conn:call({ method = "devicelist" })
    -- TODO: error handling

    ubdata = json.encode(result)
    local webresult = {}
    for i=1, #result["result"] do
        local rdata = result["result"][i]
        if not rdata["name"] then
            rdata["name"] = rdata["mac"]
        end
        rdata["lastSeen"] = rdata["lastseen"]
        webresult[rdata["mac"]] = rdata
    end
    self:set_api_headers(response)
    response.content = json.encode(webresult)
    response:set_header("Last-Modified", self.device_manager.last_update)
    return response
end

function handler:handle_rpc_call(request, response)
    self:set_api_headers(response)
    if request.method == "POST" then
        if request.post_data then
            local conn = rpc.connect()
            if not conn then
                error("failed to connect to RPC mechanism")
            end
            result, err = conn:call(request.post_data)
            if result then
                response.content = json.encode(result)
            else
                response:set_status(500, err)
            end
        else
            response:set_status(400, "Bad request")
            response.content = json.encode({status = 400, error = "Missing value: 'config' in POST data"})
        end
    else
        response:set_status(405, "Method not allowed")
    end
    return response
end

-- this *queues* a message to send to all active clients
function handler:send_websocket_update(name, arguments)
    if table.getn(self.websocket_clients) == 0 then return end
    local msg = ""
    if arguments == nil then
        msg = '{"type": "update", "name": "' .. name .. '"}'
    else
        msg = '{"type": "update", "name": "' .. name .. '", "args": ' .. json.encode(arguments) .. '}'
    end
    for i,c in pairs(self.websocket_clients) do
        print("[XX] send msg to client")
        --c:send(msg)
        c:queue_message(msg)
    end
end

-- note, client needs to be passed here (this data is only sent to the given client)
local function send_websocket_initialdata(client, name, arguments)
    local msg = ""
    if arguments == nil then
        msg = '{"type": "data", "name": "' .. name .. '"}'
    else
        msg = '{"type": "data", "name": "' .. name .. '", "args": ' .. json.encode(arguments) .. '}'
    end

    client:send(msg)
end

function handler:get_filtered_profile_list()
    local profile_list = {}
    -- we'll make a selective deep copy of the data, since for now we
    -- want to leave out some of the fields
    -- we may need to find a construct similar to the
    -- serializer from DRF
    for i,v in pairs(self.profile_manager.profiles) do
        local profile = {}
        profile.id = v.id
        profile.name = v.name
        profile.type = v.type
        profile.description = v.description
        table.insert(profile_list, profile)
    end
    for i,v in pairs(profile_list) do
      v.rules_v4 = nil
      v.rules_v6 = nil
    end
    return profile_list
end

function handler:handle_profile_list(request, response)
    self:set_api_headers(response)
    response.content = json.encode(self:get_filtered_profile_list())
    response:set_header("Last-Modified", self.profile_manager.profiles_updated)
    return response
end

function handler:handle_device_profiles(request, response, device_mac)
    self:set_api_headers(response)
    if request.method == "GET" or request.method == "HEAD" then
        local content_json, updated = self.profile_manager:get_device_profiles(device_mac)
        response.content = json.encode(content_json)
        response:set_header("Last-Modified", updated)
    else
        if request.post_data ~= nil and request.post_data.profile_id ~= nil then
            local profile_id = request.post_data.profile_id
            local status = nil
            local err = ""
            if self.device_manager:get_device_seen(device_mac) ~= nil then
                status, err = self.profile_manager:set_device_profile(device_mac, request.post_data.profile_id)
                local device_name = self.device_manager:get_device_seen(device_mac).name
                local profile_name = self.profile_manager.profiles[profile_id].name
                if status then
                  local notification_txt = "Profile set to " .. profile_name
                  self:create_notification("profile_set_to", { profile_name }, notification_txt, device_mac, device_name)
                  self:send_websocket_update("deviceProfileUpdate", { deviceName=device_name, profileName=profile_name })
                else
                  local notification_txt = "Error setting device profile: " .. err
                  self:create_notification("profile_set_error", { err }, notification_txt, device_mac, device_name)
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
        local seen = self.device_manager:device_is_new(device_mac)
        if seen == nil then
            response:set_status(400, "Bad request")
            response.content = json.encode({status = 400, error = "Unknown device: " .. device_mac})
        else
            self.device_manager:set_device_is_new(device_mac, not seen)
        end
    end
    return response
end

-- TODO: move to own module?
-- (down to TODO_MOVE_ENDS_HERE)
function handler:create_notification(msg_key, msg_args, text, device_mac, device_name)
    local new_notification = {}
    new_notification['id'] = self.notification_counter
    self.notification_counter = self.notification_counter + 1
    new_notification['timestamp'] = os.time()
    new_notification['messageKey'] = msg_key
    new_notification['messageArgs'] = msg_args
    new_notification['message'] = text
    if device_mac ~= nil then
        new_notification['deviceMac'] = device_mac
    end
    if device_name ~= nil then
        new_notification['deviceName'] = device_name
    end
    table.insert(self.notifications, new_notification)
    self.notifications_updated = spin_util.get_time_string()

    self:send_websocket_update("newNotification", new_notification)
end

function handler:delete_notification(id)
    local to_remove = nil
    for i,v in pairs(self.notifications) do
        if v.id == id then
            to_remove = i
        end
    end
    if to_remove ~= nil then
        table.remove(self.notifications, to_remove)
        self.notifications_updated = spin_util.get_time_string()
    end
end

function handler:handle_notification_list(request, response)
    self:set_api_headers(response)
    response.content = json.encode(self.notifications)
    response:set_header("Last-Modified", self.notifications_updated)
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
        local errors = {}
        if request.post_data == nil then
          table.insert(errors, "no message POST data")
        else
          if request.post_data.messageKey == nil then
            table.insert(errors, "No element 'messageKey' found in post data")
          end
          if request.post_data.message == nil then
            table.insert(errors, "No element 'message' found in post data")
          end
        end
        if #errors == 0 then
            local args = request.post_data.messageArgs
            if args == nil then args = {} end
            self:create_notification(request.post_data.messageKey, args, request.post_data.message, request.post_data.deviceMac, request.post_data.deviceName)
            return response
        else
            response:set_status(400, "Bad request")
            response.content = json.encode({status = 400, error = errors})
        end
    else
        response.set_status(400, "Bad request")
        response.content = "This URL requires a POST"
    end
    return response
end


local data_printer = function(ws)
  print("data_printer")
  local message = "{\"foo\": \"bar\"}"
  --copas.send(ws, message)
  -- any messages we want to have sent by default could go here.
  -- in principle we only send stuff from other methods
  --for i=1,3 do
  --  print("send "..i)
  --  ws:send(message)
  --  copas.sleep(1)
  --end
  --ws:close()
  return "foo"
end

local ws_opts =
{
  -- listen on port 8080
  --port = 8080,
  -- the protocols field holds
  --   key: protocol name
  --   value: callback on new connection
  protocols = {
    -- this callback is called, whenever a new client connects.
    -- ws is a new websocket instance
    echo = echo_handler
  },
  default = data_printer
}


function handler:handle_websocket(request, response)
    -- try to upgrade to a websocket connection.
    -- if successful, we add it to the list of websockets (there may be more...)
    print(request.raw_sock)
    local flat_headers = {}
    table.insert(flat_headers, request.http_line)
    for h,v in pairs(request.headers) do
        table.insert(flat_headers, h:lower() .. ": " .. v)
    end
    table.insert(flat_headers, "\r\n")
    --print("[XX] FLAT HEADERS:")
    --for fh,fv in pairs(flat_headers) do
    --    print("[XX]    " .. fv)
    --end
    --print("[XX] END OF FLAT HEADERS OF TYPE " .. type(flat_headers))
    request.raw_sock:settimeout(1)
    print("[XX] AAAAAA")
    client, err = self.ws_handler.add_client(flat_headers, request.raw_sock, request.connection, self)
    print("[XX] BBBBBB")
    if not client then
        print("[XX] CCCCCC")
        response:set_status(400, "Bad request")
        response.content = err
        return response
    else
        print("[XX] DDDDDD")
        table.insert(self.websocket_clients, status)
        -- send any initial client information here
        client:send('{"message": "hello, world"}')
        -- Send the overview of profiles
        send_websocket_initialdata(client, "profiles", self:get_filtered_profile_list())
        -- Send the overview of known devices so far, which includes their profiles
        send_websocket_initialdata(client, "devices", self.device_manager:get_devices_seen())
        -- Send all notifications
        send_websocket_initialdata(client, "notifications", self.notifications)
        print("[XX] NEW CONNECT NOW COUNT: " .. table.getn(self.websocket_clients))
        while client.state ~= 'CLOSED' do
          print("[XX] NOTCLOSED")
          --if client:has_queued_messages() then
          --  print("[XX] have messages to send!")
          --  client:send_queued_messages()
          --else
          --  copas.sleep(0.1)
          --end
          local dummy = {
            send = function() end,
            close = function() end
          }
          copas.send(dummy)
        end
    end
    print("[XX] yoyo yoyo")

    -- websocket took over the connection, return nil so minittp
    -- does not send a response
    return nil
end

function handler:have_websocket_messages()
  return table.getn(self.websocket_messages) > 0
end

function handler:do_add_ws_c(client)
  print("[XX] FOO ADDING CLINET")
  table.insert(self.websocket_clients, client)
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

    self.device_manager = device_manager_m.create(self.profile_manager)

    self.notifications = {}
    self.notifications_updated = spin_util.get_time_string()
    self.notification_counter = 1

    self.websocket_clients = {}
    self.websocket_messages = {}
    self.ws_handler = ws_ext.ws_server_create(ws_opts)

    self.mqtt_queue_msgs = {}

    -- We will use this list for the fixed url mappings
    -- Fixed handlers are interpreted as they are; they are
    -- ONLY valid for the EXACT path identified in this list
    -- (for more flexibility, see the pattern handlers below)
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
        ["/spin_api/notifications/create"] = self.handle_notification_add,
        ["/spin_api/configuration"] = self.handle_configuration,
        ["/spin_api/rpc"] = self.handle_rpc_call,
        ["/spin_api/ws"] = self.handle_websocket
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
      table.insert(h.mqtt_queue_msgs, payload)
    end

--    client.ORIG_ON_MESSAGE = function(mid, topic, payload)
--        --print("[XX] message for you, sir!")
--        local success, pd = coxpcall.pcall(json.decode, payload)
--        if success and pd then
--            if topic == TRAFFIC_CHANNEL then
--                if pd["command"] and pd["command"] == "traffic" then
--                    h:handle_traffic_message(pd["result"], payload)
--                end
--            elseif handle_incidents and topic == INCIDENT_CHANNEL then
--                if pd["incident"] == nil then
--                    print("Error: no incident data found in " .. payload)
--                    print("Incident report ignored")
--                else
--                    local incident = pd["incident"]
--                    local ts = incident["incident_timestamp"]
--                    for i=ts-5,ts+5 do
--                        if handle_incident_report(incident, i) then break end
--                    end
--                end
--            end
--        end
--    end

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
    copas.addthread(self.mqtt_queue_looper, self)

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
