
local posix = require "posix"
local wirefmt = require "wirefmt"
local netlink = require "spin_netlink"

if posix.AF_NETLINK ~= nil then
    local fd, err = netlink.connect_traffic()
    local recv_count = 0
    msg_str = "Hello!"
    hdr_str = netlink.create_netlink_header(msg_str, 0, 0, 0, netlink.get_process_id())
    
    posix.send(fd, hdr_str .. msg_str);

    while true do
        local spin_msg, err, errno = netlink.read_netlink_message(fd)
            if spin_msg then
                --wirefmt.hexdump(spin_msg)
                netlink.print_message(spin_msg)
            else
                if (errno == 105) then
                  -- try again
                else
                    fd = netlink.connect()
                end
            end
            recv_count = recv_count + 1
            if recv_count > 50 then
				posix.send(fd, hdr_str .. msg_str);
				recv_count = 0
			end
    end
else
    print("no posix.AF_NETLINK")
end

