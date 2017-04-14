--
-- Wrapper for OpenWRT-style config files
--

-- TODO: either start using /etc/firewall.user fully
-- or make a SPIN-only section in the config file (and blind-copy the
-- other parts of the config file)

local _m = {}

local mio = require 'mio'

-- Config file wrapper
--
-- local c = _m.read_config("/tmp/valibox")
-- c:print()
-- if c:updated() then print("updated") else print("not updated") end
-- print(c:get('language', 'language'))
--

local Config = {}
Config.__index = Config

local ConfigSection = {}
ConfigSection.__index = ConfigSection

function _m.create_ConfigSection(section_name)
  local section = {}
  setmetatable(section, ConfigSection)
  section.name = section_name
  -- options is a direct dict
  section.options = {}
  -- lists is a dict of lists
  section.lists = {}
  return section
end

function ConfigSection:set_name(name)
  self.name = name
end

function ConfigSection:get_name()
  return self.name
end

function ConfigSection:set_option(option_name, option_value)
  -- should we error on multiple definitions of the same option?
  self.options[option_name] = option_value
end

function ConfigSection:get_option(option_name)
  return self.options[option_name]
end

function ConfigSection:add_list_value(list_name, list_value)
  local list = self.lists[list_name]
  if list == nil then
    list = {}
  end
  table.insert(list, list_value)
  self.lists[list_name] = list
end

function ConfigSection:get_list(list_name)
  local list = self.lists[list_name]
  if list == nil then
    return {}
  else
    return list
  end
end

function ConfigSection:write(out)
  out:write("config " .. self:get_name() .. "\n")
  for option_name,value in pairs(self.options) do
    out:write("\toption " .. option_name .. " '" .. value .. "'\n")
  end
  for list_name,value in pairs(self.lists) do
    for _,list_entry in value do
      out:write("\tlist " .. list_name .. " '" .. list_entry .. "'\n")
    end
  end
end

function Config:read_config()
  self.sections = {}
  self.last_modified = self:read_last_modified()
  local cfr, err = mio.file_reader(self.filename)
  if cfr == nil then
    io.stderr:write("Error, unable to open " .. self.filename .. ": " .. err .. "\n")
    return nil, err
  end

  local current_section = _m.create_ConfigSection()

  for line in cfr:read_line_iterator() do
    local sname = line:match("config%s+(%S+)")
    local qoname,qoval = line:match("%s*option%s+(%S+)%s+'([^']+)'")
    local oname,oval = line:match("%s*option%s+(%S+)%s+([%S]+)")
    if sname then
      if current_section:get_name() then
        -- new section! store old and start new
        table.insert(self.sections, current_section)
        current_section = _m.create_ConfigSection(sname)
      else
        current_section:set_name(sname)
      end
    elseif qoname and qoval then
      if not current_section:get_name() then
        io.stderr:write("Parse error in " .. self.filename .. ": option outside of section")
        return nil
      end
      current_section:set_option(qoname, qoval)
    elseif oname and oval then
      if not current_section:get_name() then
        io.stderr:write("Parse error in " .. self.filename .. ": option outside of section")
        return nil
      end
      current_section:set_option(oname, oval)
    end
  end
  if current_section:get_name() then
    table.insert(self.sections, current_section)
  end
  cfr:close()

  return true
end

function Config:old_read_config()
  self.sections = {}
  self.last_modified = self:read_last_modified()

  local cfr, err = mio.file_reader(self.filename)
  if cfr == nil then
    io.stderr:write("Error, unable to open " .. self.filename .. ": " .. err .. "\n")
    return nil, err
  end

  local current_section = {}
  current_section.options = {}
  current_section.lists = {}
  for line in cfr:read_line_iterator() do
    local sname = line:match("config%s+(%S+)")
    local qoname,qoval = line:match("%s*option%s+(%S+)%s+'([^']+)'")
    local oname,oval = line:match("%s*option%s+(%S+)%s+([%S]+)")
    if sname then
      if current_section.name then
        table.insert(self.sections, current_section)
        current_section = {}
        current_section.options = {}
        current_section.lists = {}
        current_section.name = sname
      end
    elseif qoname and qoval then
      if not current_section_name then
        io.stderr:write("Parse error in " .. self.filename .. ": option outside of section")
        return nil
      end
      current_section[qoname] = qoval
    elseif oname and oval then
      if not current_section_name then
        io.stderr:write("Parse error in " .. self.filename .. ": option outside of section")
        return nil
      end
      current_section[oname] = oval
    end
  end
  if current_section_name then
    table.insert(self.sections)
    self.sections[current_section_name] = current_section
  end
  cfr:close()

  return true
end

function Config:read_last_modified()
  return mio.file_last_modified(self.filename)
end

function Config:print()
  for _,section in pairs(self.sections) do
    section:write(io.stdout)
    print("")
  end
end

function Config:updated(reload)
  local result = self.last_modified ~= self:read_last_modified()
  if reload then return self:read_config() else return result end
end

--
-- Direct access function
--

-- returns the first section with the given name
function Config:get_section(section_name)
  for _,section in pairs(self.sections) do
    if section:get_name() == section_name then
      return section
    end
  end
  return nil, "Unknown section: '" .. sname .. "'"
end

-- returns all sections with the given name as a list
function Config:get_sections(section_name)
  local result = {}
  for _,section in pairs(self.sections) do
    if section:get_name() == section_name then
      table.insert(result, section)
    end
  end
  return result
end

-- returns the first section where there is an option with the given
-- name that is set to the given value (for instance:
-- give me the first section where 'name' is set to 'SPIN-Block-rule-1'
function Config:get_section_by_option_value(section_name, option_name, option_value)
  for _,section in pairs(self.sections) do
    if section:get_name() == section_name and section:get_option(option_name) == option_value then
      return section
    end
  end
  return nil, "No section with option " .. option_name .. " set to '" .. option_value .."'"
end

-- returns all sections where there is an option with the given
-- name that is set to the given value (for instance:
-- give me the first section where 'name' is set to 'SPIN-Block-rule-1'
function Config:get_sections_by_option_value(section_name, option_name, option_value)
  local result = {}
  for _,section in pairs(self.sections) do
    if section:get_name() == section_name and section:get_option(option_name) == option_value then
      table.insert(result, section)
    end
  end
  return result
end

-- returns all sections where there is an option with the given
-- name that matches (string:match()) the given value (for instance:
-- give me the first section where 'name' contains 'SPIN-'
function Config:get_sections_by_option_match(section_name, option_name, option_value)
  local result = {}
  for _,section in pairs(self.sections) do
    if section:get_name() == section_name and section:get_option(option_name) then
      if section:get_option(option_name):match(option_value) then
        table.insert(result, section)
      end
    end
  end
  return result
end



-- return the value of the option with the given name in the first
-- section with the given name
function Config:get_option(section_name, option_name)
  section, err = self:get_section(section_name)
  if section == nil then
    return nil, err
  end
  local option = section:get_option(option_name)
  io.stderr:write("Warning: option '" .. option_name .. "' not found in section '" .. section_name .. "'")
  return option
end

-- return the list with the given name in the first
-- section with the given name
function Config:get_list(section_name, list_name)
  section, err = self:get_section(section_name)
  if section == nil then
    return nil, err
  end
  local list = section:get_list(list_name)
  if (#result == 0) then
    io.stderr:write("Warning: list '" .. list_name .. "' not found in section '" .. section_name .. "'")
  end
  return list
end

function _m.create_Config(filename)
  local cf = {}             -- our new object
  setmetatable(cf,Config)  -- make Account handle lookup
  cf.filename = filename
  cf.sections = {}
  return cf
end

return _m
