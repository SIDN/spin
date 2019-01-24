--
-- Class for managing tcpdump processes
--

local mt_io = require 'minittp_io'
local copas = require 'copas'

local _M = {}

local tcpdumper = {}
tcpdumper.__index = tcpdumper

function _M.create(device, response)
    local td = {}
    setmetatable(td, tcpdumper)
    -- check device for format here or at caller?
    -- should be aa:bb:cc:dd:ee:ff
    td.running = true
    td.bytes_sent = 0
    td.response = response

    local subp, err = mt_io.subprocess("tcpdump", {"-U", "-i", "br-lan", "-s", "1600", "-w", "-", "ether", "host", device}, 0, true, false, false)
    if subp == nil then
        return nil
    end
    td.subp = subp

    return td
end

function tcpdumper:read_and_send(size)
    line, err = self.subp:read_bytes(size)
    if line == nil then
        if err ~= "read timed out" then
            print("Error reading from subprocess: " .. err)
            sent, err = response:send_chunk("")
            subp:kill()
            subp:close()
            return nil, err
        end
    else
        sent, err = self.response:send_chunk(line)
        if sent == nil then
            sent, err = self.response:send_chunk("")
            print("Error sending data: " .. err)
            subp:kill()
            subp:close()
            return nil, err
        else
            -- do not count the \r\n that signals end of chunk
            self.bytes_sent = self.bytes_sent + sent - 2
            return sent - 2
        end
    end
end

function tcpdumper:run()
    while self.running do
        self:read_and_send(1600)
        copas.sleep(0.1)
    end
    self.subp:kill()
    self.subp:close()

    -- End with an empty chunk, as per transfer-encoding: chunked protocol
    sent, err = self.response:send_chunk("")
    if sent == nil then
        print("Error sending data: " .. err)
    else
        print("Sent " .. " bytes");
    end
    -- just to make sure
    self.running = false
    return nil
end

function tcpdumper:stop()
    self.running = false
end

return _M
