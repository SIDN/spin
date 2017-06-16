
local posix = require "posix"
local wirefmt = require "wirefmt"
local netlink = require "spin_netlink"

if posix.AF_NETLINK ~= nil then
    local fd, err = netlink.connect()
    msg_str = "Hello!"
    hdr_str = netlink.create_netlink_header(msg_str, 0, 0, 0, netlink.get_process_id())
    
    posix.send(fd, hdr_str .. msg_str);

    while true do
        local spin_msg, err, errno = netlink.read_netlink_message(fd)
            if spin_msg then
                --wirefmt.hexdump(spin_msg)
                netlink.print_message(spin_msg)
            else
                print("[XX] err from read_netlink_message: " .. err .. " errno: " .. errno)
                if (errno == 105) then
                  -- try again
                else
                    fd = netlink.connect()
                end
            end
    end
else
    print("no posix.AF_NETLINK")
end

