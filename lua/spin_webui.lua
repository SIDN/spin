local mt_engine = require 'minittp_engine'
local mt_io = require 'minittp_io'
local mt_util = require 'minittp_util'

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

function handler:init(args)
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

    self:load_templates()
    return true
end

function handler:handle_request(request, response)
    local result = nil
    if request.path == "/" then
        html, err = self:render_raw("index.html", {mqtt_host = self.config['mqtt']['host']})
        if html == nil then
            response:set_status(500, "Internal Server Error")
            response.content = "Template error: " .. err
            return response
        end
        response.content = html
    else
        -- try one of the static files
        response = mt_engine.handle_static_file(request, response, "../html")
    end
    return response
end

return handler
