#!/usr/bin/lua

local mqtt = require 'mosquitto'
local json = require 'json'
local http = require 'socket.http'
local https = require 'ssl.https'
local posix = require 'posix'

local INCIDENT_CHANNEL = "SPIN/incidents"

local DEFAULT_SERVER_URI = "https://spin.tjeb.nl/incidents/api/"

-- hey this could fetch it too!
-- do we have http request built in?
-- if so, fetch from known or configured address, check if correct json or translate csv,
-- then send to mqtt

-- right now, cli only for first attempt
-- also, not much input checking yet
-- src_addr and src_port are not actually used in history search by spin (might even be irrelevant)

function help(error)
    if error ~= nil then
        print("Error: " .. error)
        print("")
    end
    print("Usage: incident_report_tool.lua [options] timestamp dst_addr dst_port [src_addr] [src_port]")
    print("")
    print("Listens to incident report notifications, fetches the incident data upon notification, and sends it to SPIN")
    print("")
    print("Options:")
    print("-s <URI> server URI to fetch reports from (will query")
    print("         '<URI><timestamp from notification>'). Defaults to")
    print("         " .. DEFAULT_SERVER_URI)
    print("-l <ip address> address to listen on (defaults to *)")
    print("-p <port> port to listen on (defaults to 40421)")
    print("-m <host> mqtt host (defaults to 127.0.0.1)")
    print("-n <port> mqtt port (defaults to 1883)")
    print("-i <interval> Instead of listening for notifications, fetch them every <interval> seconds")
    os.exit()
end

-- returns:
-- mqtt_host, mqtt_port, timestamp, src_addr, src_port, dest_addr, dest_port
-- or none on error
function parse_args(args)
    local listen = true
    local mqtt_host = "127.0.0.1"
    local mqtt_port = 1883
    local argcount = 0
    local listen_host = "*"
    local listen_port = 40421
    local server_uri = DEFAULT_SERVER_URI
    local poll_interval = 0
    skip = false
    for i = 1,table.getn(args) do
        if skip then
            skip = false
        elseif arg[i] == "-h" then
            help()
        elseif arg[i] == "-s" then
            server_uri = arg[i+1]
            if server_uri == nil then help("missing argument for -m") end
            skip = true
        elseif arg[i] == "-m" then
            mqtt_host = arg[i+1]
            if mqtt_host == nil then help("missing argument for -m") end
            skip = true
        elseif arg[i] == "-n" then
            mqtt_port = tonumber(arg[i+1])
            if mqtt_host == nil then help("missing argument for -n") end
            skip = true
        elseif arg[i] == "-l" then
            listen_host = arg[i+1]
            if listen_host == nil then help("missing argument for -l") end
            skip = true
        elseif arg[i] == "-p" then
            listen_port = tonumber(arg[i+1])
            if listen_port == nil then help("missing argument for -p") end
            skip = true
        elseif args[i] == "-i" then
            poll_interval = tonumber(arg[i+1])
            if poll_interval == nil then help("missing argument for -i") end
            if poll_interval <= 0 then help("poll interval must be > 0") end
            if (listen_host ~= "*" or listen_port ~= 40421) then help("Cannot run in listen and poll mode simultaneously") end
            skip = true
        else
            help("Too many arguments at " .. table.getn(args))
        end
    end
    return server_uri, listen_host, listen_port, mqtt_host, mqtt_port, poll_interval
end

function report_incident(incident_msg_json, mqtt_host, mqtt_port)
    local client = mqtt.new()

    client.ON_CONNECT = function()
        client:publish(INCIDENT_CHANNEL, incident_msg_json)
    end

    client.ON_PUBLISH = function()
        client:disconnect()
    end

    client:connect(mqtt_host, mqtt_port)

    client:loop_forever()
end

function report_incidents(incident_msg_json, mqtt_host, mqtt_port)
    incidents = json.decode(incident_msg_json)
    for _,i in pairs(incidents) do
        report_incident(json.encode(i, mqtt_host, mqtt_port))
    end
end

function fetch_incident_data(uri)
    if uri:sub(0,8) == "https://" then
        body,c,l,h = https.request(uri)
    else
        body,c,l,h = http.request(uri)
    end
    print("Fetching incident information from " .. uri)
    print("Status response: " .. h)
    --for h,v in pairs(l) do
    --    print(h .. ": " .. v)
    --end
    -- very basic status code parsing
    if (c >= 200 and c < 300) then
        return body
    elseif (c >= 301 and c < 400) then
        return fetch_incident_data(l['location'])
    end
end

function poller(server_uri, interval, mqtt_host, mqtt_port)
    local prev_time = os.time()
    while true do
        posix.sleep(3)
        local cur_time = os.time()
        local url = server_uri .. "between/" .. prev_time .. "/" .. cur_time
        print(url)
        local incidents_json = fetch_incident_data(url)
        if incidents_json == nil then
            print("Error fetching incident data, notification not accepted")
        else
            print("should report incidents now, json:")
            print(incidents_json)
            report_incidents(incidents_json, mqtt_host, mqtt_port)
        end
        prev_time = cur_time
    end
end

function listener(server_uri, listen_host, listen_port, mqtt_host, mqtt_port)
    local socket = require("socket")
    local server = assert(socket.bind(listen_host, listen_port))
    local tcp = assert(socket.tcp())

    print(socket._VERSION)
    print(tcp)
    print("Listening on " .. listen_host .. ":" .. listen_port)
    while 1 do

        local client = server:accept()

        line = client:receive()
        local incident_timestamp = tonumber(line)
        if incident_timestamp ~= nil then
            print("Got incident report ping for timestamp " .. incident_timestamp)
            local incidents_json = fetch_incident_data(server_uri .. "incident/" .. incident_timestamp)
            if incident_msg_json == nil then
                print("Error fetching incident data, notification not accepted")
            else
                report_incidents(incidents_json, mqtt_host, mqtt_port)
            end
        else
            print("Bad message from client, ignoring notification")
        end
        client:close()

    end
end

-- cli options:
-- mqtt_host, mqtt_port, listen_host, listen_port
local server_uri, listen_host, listen_port, mqtt_host, mqtt_port, poll_interval = parse_args(arg)

if (poll_interval > 0) then
    print("[XX] polling mode")
    poller(server_uri, interval, mqtt_host, mqtt_port)
else
    listener(server_uri, listen_host, listen_port, mqtt_host, mqtt_port)
end
