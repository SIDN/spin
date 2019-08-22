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

local json = require 'json'

-- Additional supporting tools
local ws_ext = require 'ws_ext'
local tcpdumper = require 'tcpdumper'
local tcpdumper2 = require 'tcpdumper2'

local TRAFFIC_CHANNEL = "SPIN/traffic"
local HISTORY_SIZE = 600

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
    print("-h              Show this help")
    os.exit(rcode)
end

function arg_parse(args)
    config_file = nil

    if args == nil then return config_file end

    skip = false
    for i = 1,#args do
        if skip then
            skip = false
        elseif args[i] == "-h" then
            help()
        elseif args[i] == "-c" then
            config_file = args[i+1]
            if config_file == nil then help(1, "missing argument for -c") end
            skip = true
        else
            help(1, "Too many arguments at '" .. args[i] .. "'")
        end
    end

    return config_file
end

function handler:read_config(args)
    local config_file = arg_parse(args)
    local config = {}
    if config_file ~= nil then
        config, err = spin_util.config_parse(config_file)
        if config == nil then return nil, err end
    end
    self.config = config
end

function handler:handle_index(request, response)
    html, err = self:render("index.html")
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
    response:set_header("Location", "/spin_api/tcpdump?device=" .. device)
    response:set_status(302, "Found")
    return response
end

function handler:handle_tcpdump_manage2(request, response)
    local device_mac = request.params["device"]
    local running = false
    local bytes_sent = 0
    if self.active_dumps[dname] ~= nil then
        running = true
        bytes_sent = self.active_dumps[dname].bytes_sent
    end

    -- retrieve additional info about the device
    local conn, err = rpc.connect()
    if not conn then
        -- We return an HTTP 200, but with the content set to
        -- an error
        response.content = json.encode({error = err})
        return response
    end
    device_name = "unknown"
    device_ips = ""
    result, err = conn:call({ method = "list_devices", jsonrpc = "2.0", id = 1 })
    -- TODO: error handling
    if result == nil then
        print("error: " .. err)
    end
    print("[XX] RESULT:")
    print(json.encode(result))
    for i=1, #result["result"] do
        local rdata = result["result"][i]
        if rdata["mac"] == device_mac then
            if rdata["name"] then
                device_name = rdata["name"]
                device_name="AAA"
            end
            device_name="BBB"
            device_ips=table.concat(rdata["ips"], ", ")
        end
    end

    html, err = self:render_raw("mqtt.html", { device_name=device_name, device_mac=device_mac, device_ips=device_ips})
    response:set_header("Last-Modified", spin_util.get_time_string())

    if html == nil then
        response:set_status(500, "Internal Server Error")
        response.content = "Template error: " .. err
        return response
    end
    response.content = html
    return response
end

function handler:handle_tcpdump_start2(request, response)
    local device = request.params["device"]
    local dname = get_tcpdump_pname(request, device)

    if self.active_dumps[dname] ~= nil then return nil, "already running" end
    local dumper, err = tcpdumper2.create(device)
    -- todo: 500 internal server error?
    if dumper == nil then return nil, err end
    self.active_dumps[dname] = dumper

    dumper:run()
    -- remove it again
    self.active_dumps[dname] = nil
    response.content = "{}"
    return response
end

function handler:handle_tcpdump_stop2(request, response)
    local device = request.params["device"]
    local dname = get_tcpdump_pname(request, device)

    if self.active_dumps[dname] ~= nil then
        self.active_dumps[dname]:stop()
    end
    response:set_header("Location", "/spin_api/tcpdump?device=" .. device)
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

-- Retrieve the device list through an RPC call,
-- and return it as a JSON string in the old format
-- of /spin_api/devices
function handler:retrieve_device_list()
    local conn, err = rpc.connect()
    if not conn then
        -- We return an HTTP 200, but with the content set to
        -- an error
        response.content = json.encode({error = err})
        return response
    end
    result, err = conn:call({ method = "list_devices", jsonrpc = "2.0", id = 1 })
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
    return webresult
end

function handler:handle_device_list(request, response)
    print("Calling RPC")
    self:set_api_headers(response)
    response.content = json.encode(self:retrieve_device_list())
    response:set_header("Last-Modified", spin_util.get_time_string())
    return response
end

function handler:handle_rpc_call(request, response)
    self:set_api_headers(response)
    if request.method == "POST" then
        if request.post_data then
            local conn, err = rpc.connect()
            if not conn then
                -- We return an HTTP 200, but with the content set to
                -- an error
                response.content = json.encode({error = err})
                return response
            end
            print("[XX] GOT RPC COMMAND: " .. json.encode(request.post_data))
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
    if #self.websocket_clients == 0 then return end
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

function handler:handle_profile_list(request, response)
    response:set_status(403, "Not Found")
    response.content = json.encode({status = 403, error = "Device profiles have been removed"})
    return response
end

function handler:handle_device_profiles(request, response, device_mac)
    response:set_status(403, "Not Found")
    response.content = json.encode({status = 403, error = "Device profiles have been removed"})
    return response
end

function handler:handle_toggle_new(request, response, device_mac)
    response:set_status(403, "Not Found")
    response.content = json.encode({status = 403, error = "The 'new' status has been removed"})
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
    client, err = self.ws_handler.add_client(flat_headers, request.raw_sock, request.connection, self)
    if not client then
        response:set_status(400, "Bad request")
        response.content = err
        return response
    else
        table.insert(self.websocket_clients, status)
        -- send any initial client information here
        client:send('{"message": "hello, world"}')
        -- Send the overview of known devices so far
        send_websocket_initialdata(client, "devices", self:retrieve_device_list())
        -- Send all notifications
        send_websocket_initialdata(client, "notifications", self.notifications)
        print("[XX] NEW CONNECT NOW COUNT: " .. #self.websocket_clients)
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
  return #self.websocket_messages > 0
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

    self.notifications = {}
    self.notifications_updated = spin_util.get_time_string()
    self.notification_counter = 1

    self.websocket_clients = {}
    self.websocket_messages = {}
    self.ws_handler = ws_ext.ws_server_create(ws_opts)

    -- We will use this list for the fixed url mappings
    -- Fixed handlers are interpreted as they are; they are
    -- ONLY valid for the EXACT path identified in this list
    -- (for more flexibility, see the pattern handlers below)
    self.fixed_handlers = {
        ["/"] = handler.handle_index,
        ["/spin_api"] = self.handle_index,
        ["/spin_api/"] = self.handle_index,
        ["/spin_api/tcpdump2"] = self.handle_tcpdump_manage2,
        ["/spin_api/tcpdump2_start"] = self.handle_tcpdump_start2,
        ["/spin_api/tcpdump2_stop"] = self.handle_tcpdump_stop2,
        ["/spin_api/tcpdump"] = self.handle_tcpdump_manage,
        ["/spin_api/tcpdump_status"] = self.handle_tcpdump_status,
        ["/spin_api/tcpdump_start"] = self.handle_tcpdump_start,
        ["/spin_api/tcpdump_stop"] = self.handle_tcpdump_stop,
        ["/spin_api/devices"] = self.handle_device_list,
        ["/spin_api/profiles"] = self.handle_profile_list,
        ["/spin_api/notifications"] = self.handle_notification_list,
        ["/spin_api/notifications/create"] = self.handle_notification_add,
        ["/spin_api/configuration"] = self.handle_configuration,
        ["/spin_api/jsonrpc"] = self.handle_rpc_call,
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

    local h = self

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
