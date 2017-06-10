--
-- lua version of spin_config
--

local netlink = require "spin_netlink"
local wirefmt = require "wirefmt"
local posix = require "posix"

function help()
    print("usage: spin_config.lua <type> <command> [args]")
    print("Types:");
    print("- ignore: show or modify the list of addresses that are ignored");
    print("- block:  show or modify the list of addresses that are blocked");
    print("- except: show or modify the list of addresses that are not blocked");
    print("Commands:");
    print("- show:   show the addresses in the list");
    print("- add:    add address to list");
    print("- remove: remove address from list");
    print("- clear:  remove all addresses from list");
    print("If no arguments are given, show all lists");
    os.exit()
end


if (#arg == 0) then
    print("TODO: list all")
    os.exit()
end

local type_str = nil
local cmd_str = nil
local ip_strs = {}
local cmd = nil
local ips = {}

local i
for i=1,#arg do
    local val = arg[i]
    if val == "-h" or val == "-help" then
        help()
    elseif not type_str then
        type_str = val
    elseif not cmd_str then
        cmd_str = val
    else
        table.insert(ip_strs, val)
    end
end

if not type_str or not cmd_str then
    help()
end


if type_str == "ignore" then
    if cmd_str == "show" then
        cmd = netlink.spin_config_command_types.SPIN_CMD_GET_IGNORE
    elseif cmd_str == "add" then
        cmd = netlink.spin_config_command_types.SPIN_CMD_ADD_IGNORE
        if #ip_strs == 0 then help() end
    elseif cmd_str == "remove" then
        cmd = netlink.spin_config_command_types.SPIN_CMD_REMOVE_IGNORE
        if #ip_strs == 0 then help() end
    elseif cmd_str == "clear" then
        cmd = netlink.spin_config_command_types.SPIN_CMD_CLEAR_IGNORE
    else
        help()
    end
elseif type_str == "block" then
    if cmd_str == "show" then
        cmd = netlink.spin_config_command_types.SPIN_CMD_GET_BLOCK
    elseif cmd_str == "add" then
        cmd = netlink.spin_config_command_types.SPIN_CMD_ADD_BLOCK
        if #ip_strs == 0 then help() end
    elseif cmd_str == "remove" then
        cmd = netlink.spin_config_command_types.SPIN_CMD_REMOVE_BLOCK
        if #ip_strs == 0 then help() end
    elseif cmd_str == "clear" then
        cmd = netlink.spin_config_command_types.SPIN_CMD_CLEAR_BLOCK
    else
        help()
    end
elseif type_str == "except" then
    if cmd_str == "show" then
        cmd = netlink.spin_config_command_types.SPIN_CMD_GET_EXCEPT
    elseif cmd_str == "add" then
        cmd = netlink.spin_config_command_types.SPIN_CMD_ADD_EXCEPT
        if #ip_strs == 0 then help() end
    elseif cmd_str == "remove" then
        cmd = netlink.spin_config_command_types.SPIN_CMD_REMOVE_EXCEPT
        if #ip_strs == 0 then help() end
    elseif cmd_str == "clear" then
        cmd = netlink.spin_config_command_types.SPIN_CMD_CLEAR_EXCEPT
    else
        help()
    end
else
    help()
end

-- convert all ip addresses to bytestrings
for _,ip_str in pairs(ip_strs) do
    print("trying: " .. ip_str)
    local ip = wirefmt.pton_v6(ip_str)
    if not ip then
        ip = wirefmt.pton_v4(ip_str)
        if not ip then
            print("Invalid IP address: " .. ip_str)
            print("Aborting")
            os.exit(1)
        end
    end
    table.insert(ips, ip)
end

-- sends command to the config port of the spin kernel module
-- returns list of response lines, or (nil, error)
function send_command(cmd, ip)
    local response_lines = {}
    local fd = netlink.connect_config()
    local msg_str = ""
    msg_str = msg_str .. string.char(cmd)
    if ip then
        if string.len(ip) == 4 then
            msg_str = msg_str .. string.char(posix.AF_INET) .. ip
        else
            msg_str = msg_str .. string.char(posix.AF_INET6) .. ip
        end
    end
    local hdr_str = netlink.create_netlink_header(msg_str, 0, 0, 0, netlink.get_process_id())
    
    posix.send(fd, hdr_str .. msg_str);

    while true do
        local response, err = netlink.read_netlink_message(fd)
        if response == nil then
            print("Error sending command to kernel module: " .. err)
            return nil, err
        end
        local response_type = string.byte(string.sub(response, 1, 1))
        if response_type == netlink.spin_config_command_types.SPIN_CMD_IP then
            local family = string.byte(string.sub(response, 2, 2))
            local ip_str
            if family == posix.AF_INET then
                ip_str = wirefmt.ntop_v4(string.sub(response, 3, 6))
            elseif family == posix.AF_INET6 then
                ip_str = wirefmt.ntop_v6(string.sub(response, 3, 18))
            else
                print("Bad inet family: " .. family)
            end
            table.insert(response_lines, ip_str)
        elseif response_type == netlink.spin_config_command_types.SPIN_CMD_END then
            -- all good, done
            break
        elseif response_type == netlink.spin_config_command_types.SPIN_CMD_ERR then
            return nil, "error from kernel module: " .. string.sub(response, 2)
        else
            return nil, "Unknown response command type from kernel module: " .. response_type
        end
    end
    return response_lines
end

-- now send the command
if #ips > 0 then
    for _,ip in pairs(ips) do
        local response_lines, err = netlink.send_cfg_command(cmd, ip)
        if response_lines then
            for _,line in pairs(response_lines) do
                print(line)
            end
        end
    end
else
    local response_lines, err = netlink.send_cfg_command(cmd)
    if response_lines then
        for _,line in pairs(response_lines) do
            print(line)
        end
    end
end

