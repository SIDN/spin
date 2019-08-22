--
-- Class for managing tcpdump processes
--

local mt_io = require 'minittp_io'
local copas = require 'copas'
local mqtt = require 'mosquitto'

local _M = {}

local tcpdumper = {}
tcpdumper.__index = tcpdumper

-- Tries to find the given mac 
function find_interface_name(mac_address)
    local proc = mt_io.subprocess("ip", { "neigh" }, 0, true)
    for line in proc:read_line_iterator() do
        tokens = line:split(" ")
        -- <ip> dev <iface> lladdr <mac> <status>
        if tokens[5] == mac_address then
            return tokens[3]
        end
    end
    return nil, "mac address not found in ip neighbours"
end

function _M.create(device)
    local td = {}
    setmetatable(td, tcpdumper)
    -- check device for format here or at caller?
    -- should be aa:bb:cc:dd:ee:ff
    td.running = true
    td.bytes_sent = 0
    td.response = response

    local mqtt_host = "localhost"
    local mqtt_port = 1883
    local mqtt_channel = "SPIN/tcpdump/" .. device

    local mqtt_client, err = mqtt.new()
    mqtt_client.ON_CONNECT = function()
        vprint("Connected to MQTT broker")
        vprint("Will publish to channel: " .. mqtt_channel)
        -- we don't need to listen, do we? only send.
        --client:subscribe(TRAFFIC_CHANNEL)
        --vprint("Subscribed to " .. TRAFFIC_CHANNEL)
    end

    mqtt_client:connect(mqtt_host, mqtt_port)

    td.mqtt_channel = mqtt_channel
    td.mqtt_client = mqtt_client

    -- We need to figure out which interface to capture on
    

    -- can we simply leave out the device? we filter on mac anyway;
    -- how does tcpdump determine where to listen?
    local iface, err = find_interface_name(device)
    if iface == nil then
        return nil, err
    end
    local subp, err = mt_io.subprocess("tcpdump", {"-U", "-i", iface, "-s", "1600", "-w", "-", "ether", "host", device}, 0, true, false, false)
    if subp == nil then
        return nil, "Unable to start tcpdump process"
    end
    td.subp = subp

    return td
end

function string.tohex(str)
    if str == nil then
        vprint("nilstring in data!")
        return nil
    else
        return (str:gsub('.', function (c)
            return string.format('%02X', string.byte(c))
        end))
    end
end

function tcpdumper:read_and_send(size)
    line, err = self.subp:read_bytes(size)
    if line == nil then
        if err ~= "read timed out" then
            print("Error reading from subprocess: " .. err)
            --sent, err = response:send_chunk("")
            self.subp:kill()
            self.subp:close()
            self.mqtt_client:disconnect()
            return nil, err
        end
    else
        --vprint("publishing data: " .. self.mqtt_client)
        vprint("to " .. self.mqtt_channel)
        local hex_line = line:tohex()
        --sent, err = self.mqtt_client:publish(self.mqtt_channel, line, qos, retain)
        sent, err = self.mqtt_client:publish(self.mqtt_channel, hex_line, qos, retain)
        vprint("published data: " .. hex_line)
        if sent == nil then
            vprint("sent: nil")
        else
            vprint("sent:")
            vprint(tostring(sent))
        end
        if err == nil then
            vprint("err: nil")
        else
            vprint("err:")
            vprint(err)
        end
        --if sent == nil then
        --    --sent, err = self.response:send_chunk("")
        --    print("Error sending data2: " .. err)
        --    self.subp:kill()
        --    self.subp:close()
        --    self.mqtt_client:disconnect()
        --    return nil, err
        --else
        --    -- do not count the \r\n that signals end of chunk
        --    self.bytes_sent = self.bytes_sent + sent - 2
        --    return sent - 2
        --end
    end
end

function tcpdumper:run()
    while self.running do
        self:read_and_send(1600)
        self.mqtt_client:loop()
        copas.sleep(0.1)
    end
    self.subp:kill()
    self.subp:close()
    self.mqtt_client:disconnect()

    -- should we send a final 'done' message, just to be sure?

    self.running = false
    return nil
end

function tcpdumper:stop()
    self.running = false
end

return _M
