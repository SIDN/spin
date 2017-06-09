
local posix = require "posix"
local netlink = require "spin_netlink"

function hexdump(data)
    local i
    io.stdout:write("00: ")
    for i=1,#data do
        if (i>1 and (i-1)%10 == 0) then
          io.stdout:write(string.format("\n%2d: ", i-1))
        end
		io.stdout:write(string.format("%02x ", string.byte(data:sub(i))))
    end
    io.stdout:write("\n")
end

if posix.AF_NETLINK ~= nil then
	local fd, err = netlink.connect()
	msg_str = "Hello!"
	hdr_str = netlink.create_netlink_header(msg_str, 0, 0, 0, netlink.get_process_id())
	
	posix.send(fd, hdr_str .. msg_str);

	while true do
	    local spin_msg, err, errno = netlink.read_netlink_message(fd)
            if spin_msg then
				--hexdump(spin_msg)
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

