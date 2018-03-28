var client = 0;// = new Paho.MQTT.Client("valibox.", 1884, "Web-" + Math.random().toString(16).slice(-5));
//var client = new Paho.MQTT.Client("127.0.0.1", 1884, "clientId");
var last_traffic = 0 // Last received traffic trace
var time_sync = 0; // Time difference (seconds) between server and client
var active = false; // Determines whether we are active or not

var datacache = []; // array of all data items to be added on the next iteration.

function init() {
    connectToMQTT();
    initGraphs();

    // set callback handlers
    client.onConnectionLost = onTrafficClose;
    client.onMessageArrived = onMessageArrived;

    // connect the client
    client.connect({onSuccess:onTrafficOpen});

    // Make smooth traffic graph when no data is received
    setInterval(redrawTrafficGraph, 1000);
}

function connectToMQTT() {
    var url = new URL(window.location);
    var mqtt_host = url.searchParams.get("mqtt_host");
    if (!mqtt_host) {
        mqtt_host = "valibox.";
    }
    var mqtt_port = url.searchParams.get("mqtt_port");
    if (mqtt_port) {
        mqtt_port = parseInt(mqtt_port);
    } else {
        mqtt_port = 1884;
    }
    client = new Paho.MQTT.Client(mqtt_host, mqtt_port, "Web-" + Math.random().toString(16).slice(-5));
}

// called when a message arrives
function onMessageArrived(message) {
    //console.log("SPIN/traffic message:"+message.payloadString);
    onTrafficMessage(message.payloadString);
}

function sendCommand(command, argument) {
    var cmd = {}
    cmd['command'] = command;
    cmd['argument'] = argument;
    //console.log("sending command: '" + command + "' with argument: '" + JSON.stringify(argument) + "'");

    var json_cmd = JSON.stringify(cmd);
    var message = new Paho.MQTT.Message(json_cmd);
    message.destinationName = "SPIN/commands";
    client.send(message);
    console.log("Sent to SPIN/commands: " + json_cmd)
}

function writeToScreen(element, message) {
    var el = document.getElementById(element);
    el.innerHTML = message;
}

function onTrafficMessage(msg) {
    try {
        var message = JSON.parse(msg)
        var command = message['command'];
        var argument = message['argument'];
        var result = message['result'];
        //console.log("debug: " + evt.data)
        switch (command) {
            case 'arp2ip': // looking in arptable of route to find matching IP-addresses
                //console.log("issueing arp2ip command");
                var node = nodes.get(selectedNodeId);
                if (node && node.address == argument) {
                    writeToScreen("ipaddress", "IP(s): " + result);
                }
                break;
            case 'traffic':
                //console.log("Got traffic command: " + msg);
                //console.log("handling trafficcommand: " + evt.data);

                // First, update time_sync to account for timing differences
                time_sync = Math.floor(Date.now()/1000 - new Date(result['timestamp']))
                // update the Graphs
                //handleTrafficMessage(result);
                datacache.push(result); // push to cache
                break;
            case 'blocked':
                //console.log("Got blocked command: " + msg);
                handleBlockedMessage(result);
                break;
            case 'dnsquery':
                //console.log("Got blocked command: " + msg);
                handleDNSQueryMessage(result);
                break;
            case 'filters':
                console.log("Got filters command: " + msg);
                filterList = result;
                filterList.sort();
                updateFilterList();
                break;
            case 'blocks':
                console.log("Got blocks command: " + msg);
                blockList = result;
                blockList.sort();
                updateBlockList();
                break;
            case 'alloweds':
                console.log("Got alloweds command: " + msg);
                allowedList = result;
                allowedList.sort();
                updateAllowedList();
                break;
            case 'nodeUpdate':
                console.log("Got node update command: " + msg);
                // just addNode?
                //updateNode(result);
                break;
            case 'serverRestart':
                serverRestart();
                break;
            default:
                console.log("unknown command from server: " + msg);
                break;
        }
    } catch (error) {
        console.error("Error handling message: " + msg);
        if (error.stack) {
            console.error("Stacktrace: " + error.stack);
        } else {
            console.error("Error: " + error);
        }
    }
}

function onTrafficOpen(evt) {
    // Once a connection has been made, make a subscription and send a message..
    console.log("Connected");
    client.subscribe("SPIN/traffic");

    sendCommand("get_filters", {})//, "")
    sendCommand("get_blocks", {})//, "")
    sendCommand("get_alloweds", {})//, "")
    //show connected status somewhere
    $("#statustext").css("background-color", "#ccffcc").text("Connected");
    active = true;
}

function onTrafficClose(evt) {
    //show disconnected status somewhere
    $("#statustext").css("background-color", "#ffcccc").text("Not connected");
    console.error('Websocket has disappeared');
    console.error(evt.errorMessage)
    active = false;
}

function onTrafficError(evt) {
    //show traffick errors on the console
    console.error('WebSocket traffic error: ' + evt.data);
}

function initTrafficDataView() {
    var data = { 'timestamp': Math.floor(Date.now() / 1000),
                 'total_size': 0, 'total_count': 0,
                 'flows': []}
    handleTrafficMessage(data);
}

// Takes data from cache and redraws the graph
// Sometimes, no data is received for some time
// Fill that void by adding 0-value datapoints to the graph
function redrawTrafficGraph() {
    // FIXME
    if (active && datacache.length == 0) {
        var data = { 'timestamp': Math.floor(Date.now() / 1000) - time_sync,
                     'total_size': 0, 'total_count': 0,
                     'flows': []}
        handleTrafficMessage(data);
    } else if (active) {
        var d = datacache;
        datacache = [];
        handleTrafficMessage(d);
    }
}

// other code goes here
// Update the Graphs (traffic graph and network view)
function handleTrafficMessage(data) {
    // update to report new traffic
    // Do not update if we have not received data yet
    if (!(last_traffic == 0 && data['total_count'] == 0)) {
        last_traffic = Date.now();
    } else {
        moveTimeline();
    }
    var aData = Array.isArray(data) ? data : [data];
    var elements = [];
    var elements_cnt = [];
    var timestamp_max = Date.now()/1000 - 60*5; // 5 minutes in the past
    while (dataitem = aData.shift()) {
        var timestamp = dataitem['timestamp']
        if (timestamp > timestamp_max) {
            timestamp_max = timestamp;
        }
        var d = new Date(timestamp * 1000);
        elements.push({
            x: d,
            y: dataitem['total_size'],
            group: 0
        });
        elements_cnt.push({
            x: d,
            y: dataitem['total_count'],
            group: 1
        });

        // Add the new flows
        var arr = dataitem['flows'];
        for (var i = 0, len = arr.length; i < len; i++) {
            var f = arr[i];
            // defined in spingraph.js
            //alert("FIND NODE: " + f['from'])
            var from_node = f['from'];
            var to_node = f['to'];

            if (from_node != null && to_node != null) {
                addFlow(timestamp + time_sync, from_node, to_node, f['count'], f['size']);
            } else {
                console.error("partial message: " + JSON.stringify(data))
            }
        }
    }
    traffic_dataset.add(elements.concat(elements_cnt));
    // console.log("Graph updated")
    // var ids = traffic_dataset.getIds();

    var graph_end = Date.parse(graph2d_1.options.end);
    if (Date.now() + 10000 >= graph_end) {
        moveTimeline(timestamp_max);
    }
}

// Function to redraw the graph
function moveTimeline(maxtime) {
    // Only rescale graph every minute. Rescale if current date gets within 10 seconds of maximum
    if (typeof maxtime == 'undefined') {
        maxtime = Date.now()/1000;
    }
    var start = Date.parse(graph2d_1.options.start);
    
    var options = {
        start: Date.now() - 700000 > start ? new Date(Date.now() - 600000) : graph2d_1.options.start,
        end: start > Date.now() - 600000 ? graph2d_1.options.end : new Date(maxtime*1000),
        height: '140px',
        drawPoints: false,
        dataAxis: {
            left: {
                range: {min: 0}
            },
            right: {
                range: {min: 0}
            }
        }
    };
    graph2d_1.setOptions(options);
}

function handleBlockedMessage(data) {
    var from_node = data['from'];
    var to_node = data['to'];
    addBlocked(from_node, to_node);
}

function handleDNSQueryMessage(data) {
    var from_node = data['from'];
    var dns_node = data['queriednode'];
    addDNSQuery(from_node, dns_node);
}

function serverRestart() {
    location.reload(true);
}

window.addEventListener("load", init, false);
