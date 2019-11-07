--
-- Assorted utility functions for spin_webui
--

local sys_stat = require "posix.sys.stat"
local mt_io = require 'minittp_io'

local _M = {}

-- returns a straight list of all the whitespace-separated tokens
-- in the given filename
-- or nil, err on error
function _M.file_tokenize(filename)
    local result = {}
    local fr, err = mt_io.file_reader(filename)
    if fr == nil then return nil, err end

    for line in fr:read_line_iterator(true) do
        for token in line:gmatch("%S+") do table.insert(result, token) end
    end

    return result
end

function _M.file_tokenize_iterator(filename)
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
            if self.index > #self.data then self.done = true end
            return value
        else
            return nil
        end
    end
    return result
end

function _M.strip_quotes(value)
    if value:len() == 1 then return value end
    if value:startswith("'") and value:endswith("'") then
        return value:sub(2, value:len()-1)
    elseif value:startswith('"') and value:endswith('"') then
        return value:sub(2, value:len()-1)
    else
        return value
    end
end

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
-- very basic config parser; hardly any checking
function _M.config_parse(filename)
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

-- return the current time, or given timestamp in the format of RFC7232 section 2.2
function _M.get_time_string(timestamp)
    if timestamp ~= nil then
      return os.date("%a, %d %b %Y %X %z", timestamp)
    else
      return os.date("%a, %d %b %Y %X %z")
    end
end

-- return the file timestamp in the format of RFC7232 section 2.2
function _M.get_file_timestamp(file_path)
    local fstat, err = sys_stat.stat(file_path)
    if fstat == nil then
        return nil, err
    end

    return os.date("%a, %d %b %Y %X %z", fstat.st_mtime)
end

return _M
