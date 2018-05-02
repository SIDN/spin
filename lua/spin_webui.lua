local mt_engine = require 'minittp_engine'
local mt_io = require 'minittp_io'
local mt_util = require 'minittp_util'

local copas = require 'copas'
local liluat = require 'liluat'

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
    print("[XX] tokenizing " .. filename)
    local result = {}
    local fr, err = mt_io.file_reader(filename)
    if fr == nil then return nil, err end
    
    for line in fr:read_line_iterator(true) do
        for token in line:gmatch("%S+") do table.insert(result, token) end
    end
    
    print("[XX] done tokenizing " .. filename)
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
    -- TODO: check correctness of config file, if any
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

function handler:init(args)
   
    -- we keep track of active downloads by having a dict of
    -- "<client_ip>-<device mac>" -> <bytes_sent>
    self.active_dumps = {}

    self:read_config(args)
    self:load_templates()

    return true
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

    local subp, err = mt_io.subprocess("tcpdump", {"-s", "0", "-w", "-", "ether", "host", "E4:95:6E:40:66:ED"}, 0, true, false, false)
    --local subp, err = mt_io.subprocess("/home/jelte/repos/minittp/examples/data_outputter.sh", {device}, 0, true, false, false)
    if subp == nil then
        print("[XX] error starting process: " .. err)
        return nil
    end
    td.subp = subp

    return td
end

function tcpdumper:run()
    while self.running do
        copas.sleep(0.1)
        line, err = self.subp:read_line(false)
        if line == nil then
            print("[XX] error reading from subprocess: " .. err)
            if err ~= "read timed out" then
                sent, err = response:send_chunk("")
                print("not timeout error")
                subp:kill()
                subp:close()
                return
            end
        else
            sent, err = self.response:send_chunk(line)
            if sent == nil then
                sent, err = self.response:send_chunk("")
                print("Error sending data: " .. err)
                subp:kill()
                subp:close()
                return nil
            else
                -- do not count the \r\n that signals end of chunk
                self.bytes_sent = self.bytes_sent + sent - 2
            end
        end
    end
    self.subp:kill()
    self.subp:close()
    sent, err = self.response:send_chunk("")
    if sent == nil then
        print("Error sending data: " .. err)
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
        print("[XX] RUNNING: " .. dname)
        running = true
        bytes_sent = self.active_dumps[dname].bytes_sent
    else
        print("[XX] NOT RUNNING: " .. dname)
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
    response:set_header("Location", "/tcpdump?device=" .. device)
    response:set_status(302, "Found")
    return response
end

function handler:handle_tcpdump_manage(request, response)
    local device = request.params["device"]
    local dname = get_tcpdump_pname(request, device)
    local running = false
    local bytes_sent = 0
    if self.active_dumps[dname] ~= nil then
        print("[XX] RUNNING: " .. dname)
        running = true
        bytes_sent = self.active_dumps[dname].bytes_sent
    else
        print("[XX] NOT RUNNING: " .. dname)
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

function handler:handle_request(request, response)
    local result = nil
    if request.path == "/" then
        return self:handle_index(request, response)
    elseif request.path == "/tcpdump" then
        return self:handle_tcpdump_manage(request, response)
    elseif request.path == "/tcpdump_status" then
        return self:handle_tcpdump_status(request, response)
    elseif request.path == "/tcpdump_start" then
        return self:handle_tcpdump_start(request, response)
    elseif request.path == "/tcpdump_stop" then
        return self:handle_tcpdump_stop(request, response)
    else
        -- try one of the static files
        response = mt_engine.handle_static_file(request, response, "/www/spin")
    end
    return response
end

return handler
