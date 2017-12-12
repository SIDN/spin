-- read a mud description json structure,
-- translate keywords to IP addresses
-- pass a packet to evaluate, return matches or not matches

cjson = require 'cjson'

--
-- helper functions. todo: put in util?
--
function has_value(tab, el)
    for _,v in pairs(tab) do
        if el == v then return true end
    end
    return false
end


-- main module
local luamud = {}

-- MUD description encapsulation
-- mud.data will contain the raw MUD (ietf-mud:mud) data
-- mud.acls will contain the encapsulated access lists defined
-- on the top-level of the MUD description. Note that these
-- can in theory contain more than defined in the actual policies
-- that are set
-- The actual policies are the acls in from_device_acls and
-- to_device_acls
--
-- This implementation follows
-- https://tools.ietf.org/html/draft-ietf-opsawg-mud-13
local mud = {}
mud.__index = mud

function luamud.mud_create_from_file(file)
    local f, err = io.open(file, "r")
    if f == nil then return nil, err end
    local f = assert(io.open(file, "rb"))
    local content = f:read("*all")
    f:close()
    new_mud, err = luamud.mud_create(content)
    return new_mud, err
end

function luamud.mud_create(mud_json)
    local new_mud = {}
    setmetatable(new_mud, mud)
    mud.data, err = cjson.decode(mud_json)
    if mud.data == nil then return nil, err end

    -- validate top-level info
    new_mud, err = new_mud:validate()
    if new_mud == nil then return nil, err end

    -- read the acls from ietf-access-control-list:access-lists
    -- and the from- and to-device policies
    acl_count, err = new_mud:read_acls()
    if acl_count == nil then return nil, err end

    return new_mud
end

-- returns the number of acls read, or nil,error
function mud:read_acls()
    self.acls = {}
    if self.data["ietf-access-control-list:access-lists"] == nil then
        -- just leave empty or error?
        return nil, "Error, no element 'ietf-access-control-list:access-lists'"
    end
    local acl_list = self.data["ietf-access-control-list:access-lists"]["acl"]
    if acl_list == nil then
        return nil, "Error, no element 'acl' in ietf-access-control-list:access-lists"
    end
    for _, acl_data in pairs(acl_list) do
        acl_entry, err = luamud.acl_create(acl_data)
        if acl_entry == nil then return nil, err end
        table.insert(self.acls, acl_entry)
    end
    print(self.data["ietf-mud:mud"]["from-device-policy"])

    self.from_device_acls = {}
    if self.data["ietf-mud:mud"]["from-device-policy"] and
       self.data["ietf-mud:mud"]["from-device-policy"]["access-lists"] and
       self.data["ietf-mud:mud"]["from-device-policy"]["access-lists"]["access-list"] then
        for _,policy_acl_desc in pairs(self.data["ietf-mud:mud"]["from-device-policy"]["access-lists"]["access-list"]) do
            local pname = policy_acl_desc["acl-name"]
            local ptype = policy_acl_desc["acl-type"]
            if self:get_acl(pname) == nil then
                return nil, "from-device ACL named '" .. pname .. "' not defined"
            end
            self.from_device_acls[pname] = ptype
        end
    end

    self.to_device_acls = {}
    if self.data["ietf-mud:mud"]["to-device-policy"] and
       self.data["ietf-mud:mud"]["to-device-policy"]["access-lists"] and
       self.data["ietf-mud:mud"]["to-device-policy"]["access-lists"]["access-list"] then
        for _,policy_acl_desc in pairs(self.data["ietf-mud:mud"]["to-device-policy"]["access-lists"]["access-list"]) do
            local pname = policy_acl_desc["acl-name"]
            local ptype = policy_acl_desc["acl-type"]
            if self:get_acl(pname) == nil then
                --return nil, "to-device ACL named '" .. pname .. "' not defined"
            end
            self.to_device_acls[pname] = ptype
        end
    end

    return table.getn(self.acls)
end

-- Validates and returns self if valid
-- Returns nil, err if not
function mud:validate()
    -- the main element should be "ietf-mud:mud"
    mud_data = self.data["ietf-mud:mud"]
    if mud_data == nil then
        return nil, "Top-level element not 'ietf-mud:mud'"
    end
    -- must contain the following elements:
    -- mud-url, systeminfo,
    if mud_data["mud-url"] == nil then
        return nil, "No element 'mud-url'"
    end
    if mud_data["last-update"] == nil then
        return nil, "No element 'last-update'"
    end
    -- cache-validity is optional, default returned by getter
    if mud_data["is-supported"] == nil then
        return nil, "No element 'is-supported'"
    end
    -- systeminfo is optional and has no default
    -- extensions is optional and has no default (should we set empty list?)

    -- check if the acls in the policies are actually defined


    --return "validation not implemented"
    return self
end

-- essentially, all direct functions are helper functions, since
-- we keep the internal data as a straight conversion from json
function mud:get_mud_url()
    return self.data["ietf-mud:mud"]["mud-url"]
end

-- warning: returns a string
function mud:get_last_update()
    return self.data["ietf-mud:mud"]["last-update"]
end

function mud:get_cache_validity()
    if self.data["ietf-mud:mud"]["cache-validity"] ~= nil then
        return self.data["ietf-mud:mud"]["cache-validity"]
    else
        return 48
    end
end

function mud:get_is_supported()
    return self.data["ietf-mud:mud"]["is-supported"]
end

function mud:get_systeminfo()
    if self.data["ietf-mud:mud"]["systeminfo"] ~= nil then
        return self.data["ietf-mud:mud"]["systeminfo"]
    else
        return nil
    end
end

function mud:get_acls()
    return self.acls
end

function mud:get_acl(name)
    for _,acl in pairs(self.acls) do
        if acl:get_name() == name then return acl end
    end
    return nil
end

-- this just returns the table acl_name->acl_type
-- NOT the actual acls (to get those, use mud:get_acl(name)
function mud:get_from_device_acls()
    return self.from_device_acls
end

-- this just returns the table acl_name->acl_type
-- NOT the actual acls (to get those, use mud:get_acl(name)
function mud:get_to_device_acls()
    return self.to_device_acls
end

function mud:to_json()
    return cjson.encode(self.data)
end


--
-- ACL encapsulation
-- perhaps we should move this to its own module
--
-- This is the element of which a list is defined in
-- ietf-access-control-list:access-lists/acl
--
-- This implementation follows
-- https://tools.ietf.org/html/draft-ietf-opsawg-mud-13
-- which extends the model at
-- https://tools.ietf.org/html/draft-ietf-netmod-acl-model-14
local acl = {}
acl.__index = acl

-- Create an acl structure using raw objects (ie the json data that
-- has already been decoded)
-- The data object is the element that is defined with the name
-- ietf-access-control-list:access-lists
function luamud.acl_create(data)
    local new_acl = {}
    setmetatable(new_acl, acl)
    new_acl.data = data
    new_acl.rules = {}
    new_acl, err = new_acl:validate()
    if new_acl == nil then return nil, err end
    return new_acl
end

function acl:validate()
    -- the top-level element should be "acl"
    if self.data["acl-name"] == nil then
        return nil, "No element 'acl-name' in access control list"
    end
    if self.data["acl-type"] == nil then
        return nil, "No element 'acl-type' in access control list"
    end
    -- todo: check type against enum
    local entries = self.data["access-list-entries"]
    if entries == nil then
        return nil, "No element 'access-list-entries' in access control list"
    end
    local ace = entries["ace"]
    if entries == nil then
        return nil, "No element 'ace' in access-list-entries"
    end
    for _, ace_e in pairs(ace) do
        -- todo: should this be its own object as well?
        local rule, err = luamud.rule_create(ace_e)
        if rule == nil then return nil, err end
        table.insert(self.rules, rule)
    end
    --if self.data
    return self
end

function acl:get_name()
    return self.data["acl-name"]
end

function acl:get_type()
    return self.data["acl-type"]
end

function acl:get_rules()
    return self.rules
end

--
-- end of ACL encapsulation
--

--
-- Rule encapsulation
--
-- todo: make this into separate module?
--
local rule = {}
rule.__index = rule

luamud.acl_types = {
    "any-acl",
    "mud-acl",
    "icmp-acl",
    "ipv6-acl",
    "tcp-acl",
    "any-acl",
    "udp-acl",
    "ipv4-acl",
    "ipv6-acl"
}

luamud.action_types = {
    "forwarding"
}

luamud.actions = {
    "accept",
    "reject",
    "drop"
}

-- 'supported' for now; we will implement them one by one
luamud.supported_match_types = {
    "ietf-mud:direction-initiated",
    "ietf-acldns:src-dnsname",
    "ietf-acldns:dst-dnsname",
    "protocol",
    "source-port-range",
    "destination-port-range",
}

-- Create an acl structure using raw objects (ie the json data that
-- has already been decoded)
-- The data object is the list element under "ace"
function luamud.rule_create(data, rule_type)
    local new_rule = {}
    setmetatable(new_rule, rule)
    rule.data = data
    rule.type = rule_type
    rule.matches = {}
    rule.actions = {}
    rule, err = rule:validate()
    if rule == nil then return nil, err end
    return rule
end

function rule:validate()
    if self.data["rule-name"] == nil then
        return nil, "No element 'rule-name' in rule"
    end
    if self.data["matches"] == nil then
        return nil, "No element 'matches' in rule " .. self.data["rule-name"]
    end
    -- acl should be one of: any-acl, mud-acl, icmp-acl, ipv6-acl,
    -- tcp-acl, any-acl, udp-acl, ipv4-acl, and ipv6-acl
    for acl_type, acl_matches in pairs(self.data["matches"]) do
        if not has_value(luamud.acl_types, acl_type) then
            return nil, "Unknown match type: " .. match_type
        end
        for match_type, match_value in pairs(acl_matches) do
            print("[XX] " .. match_type)
            if not has_value(luamud.supported_match_types, match_type) then
                return nil, "Unsupported match type: " .. match_type
            end
            if match_type == "ietf-mud:direction-initiated" then
                if not has_value({"from-device", "to-device"}, match_value) then
                    return nil, "Bad value for ietf-mud:direction_initiated: " .. match_value
                end
            elseif match_type == "ietf-acldns:src-dnsname" then
                -- what to check here?
            elseif match_type == "ietf-acldns:dst-dnsname" then
                -- what to check here?
            elseif match_type == "protocol" then
                if not type(match_value) == "number" then
                    return nil, "Bad value for protocol match rule: " .. match_value
                end
            elseif match_type == "source-port-range" then
                print("[XX]" .. type(match_value['lower-port']))
                -- should be a table with 'lower-port' (int, mandatory),
                -- 'upper-port' (int, optional), and 'operation' (optional)
                if match_value['lower-port'] == nil then
                    return nil, "Missing 'lower-port' value in source-port-range match rule"
                elseif type(match_value['lower-port']) ~= "number" then
                    return nil, "Bad value for source-port-range lower-port: " .. match_value['lower-port']
                end
                if match_value['upper-port'] ~= nil and
                   type(match_value['upper-port']) ~= "number" then
                    return nil, "Bad value for source-port-range upper-port: " .. match_value['upper-port']
                end
            elseif match_type == "destination-port-range" then
                print("[XX]" .. type(match_value['lower-port']))
                -- should be a table with 'lower-port' (int, mandatory),
                -- 'upper-port' (int, optional), and 'operation' (optional)
                if match_value['lower-port'] == nil then
                    return nil, "Missing 'lower-port' value in source-port-range match rule"
                elseif type(match_value['lower-port']) ~= "number" then
                    return nil, "Bad value for source-port-range match rule: " .. match_value['lower-port']
                end
                if match_value['upper-port'] ~= nil and
                   type(match_value['upper-port']) ~= "number" then
                    return nil, "Bad value for source-port-range upper-port: " .. match_value['upper-port']
                end
            --elseif match_type == "" then
            end
        end
    end
    -- TODO: further match rules; dnsname, port-range, dscp, etc.
    -- also check for specifics in list...

    if self.data["actions"] == nil then
        return nil, "No element 'actions' in rule " .. self.data["rule-name"]
    end
    for action_type, action in pairs(self.data["actions"]) do
        if not has_value(luamud.action_types, action_type) then
            return nil, "Unknown action type: " .. action_type
        end
        --if action_type == "ietf-mud:direction-initiated" and
        --   not has_value(luamud.action_direction_initiated, action_

    end
    return self
end

function rule:get_name()
    return self.data["rule-name"]
end

function rule:get_matches()
    return self.matches
end

function rule:get_actions()
    return self.actions
end

return luamud
