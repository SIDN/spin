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

    -- read the acls from ietf-access-control-list:access-lists
    acl_count, err = new_mud:read_acls()
    if acl_count == nil then return nil, err end

    mud, err = mud:validate()
    if mud == nil then return nil, err end
    return mud
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
    return table.getn(self.acls)
end

-- Validates and returns self if valid
-- Returns nil, err if not
function mud:validate()
    print("[XX] validating MUD description")
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

function mud:get_acl(name)
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
    acl.data = data
    acl.rules = {}
    acl, err = acl:validate()
    if acl == nil then return nil, err end
    return acl
end

function acl:validate()
    -- the top-level element should be "acl"
    print("[XX] validating ACL")
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

luamud.match_types = {
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
    "ietf-mud:direction-initiated",
    "forwarding"
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
    for match_type, match_match in pairs(self.data["matches"]) do
        if not has_value(luamud.match_types, match_type) then
            return nil, "Unknown match type: " .. match_type
        end
    end
    -- TODO: further match rules; dnsname, port-range, etc.

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

return luamud
