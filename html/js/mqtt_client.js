var client = new Paho.MQTT.Client("192.168.8.1", 1884, "clientId");

var node_cache = {}

function init() {
    initGraphs();

    // set callback handlers
    client.onConnectionLost = onTrafficClose;
    client.onMessageArrived = origonMessageArrived;

    // connect the client
    client.connect({onSuccess:onTrafficOpen});
}

// called when the client connects
/*
function origonConnect() {
    alert("Connect")
    // Once a connection has been made, make a subscription and send a message.
    console.log("onConnect");
    client.subscribe("SPIN-output-all");
    message = new Paho.MQTT.Message("Hello");
    message.destinationName = "SPIN-Config";
    client.send(message);
}

// called when the client loses its connection
function origonConnectionLost(responseObject) {
    if (responseObject.errorCode !== 0) {
        console.log("onConnectionLost:"+responseObject.errorMessage);
    }
}
*/
// called when a message arrives
function origonMessageArrived(message) {
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
    console.log("Sent: " + json_cmd)
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
    console.log("Sent: " + json_cmd)
}

function sendCommandDNS(command, argument) {
    var cmd = {}
    cmd['command'] = command;
    cmd['argument'] = argument;
    //console.log("sending command: '" + command + "' with argument: '" + JSON.stringify(argument) + "'");

    var json_cmd = JSON.stringify(cmd);
    var message = new Paho.MQTT.Message(json_cmd);
    message.destinationName = "SPIN/dnsnames";
    client.send(message);
    console.log("Sent: " + json_cmd)
}

function writeToScreen(element, message) {
    var el = document.getElementById(element);
    el.innerHTML = message;
}

function onTrafficMessage(msg) {
    //try {
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
            case 'ip2hostname':
                //console.log("issueing ip2hostname command");
                var node = nodes.get(selectedNodeId);
                if (node && node.address == argument) {
                    writeToScreen("reversedns", "Reverse DNS: " + result);
                }
                // update_ip_nodes
                if (result != "No reverse name found") {
                    nodeNames[argument] = result;
                    for (ip in nodeIds) {
                        if (ip == argument) {
                            var nodeId = getNodeId(ip)
                            var node = nodes.get(nodeId);
                            node.label = result;
                            nodes.update(node);
                        }
                    }
                }
                break;
            case 'traffic':
                //console.log("handling trafficcommand: " + evt.data);
                // update the Graphs
                handleTrafficMessage(result);
                break;
            case 'blocked':
                console.log("Got blocked command: " + msg);
                handleBlockedMessage(result);
                break;
            case 'names':
                //nodeNames = result;
                break;
            case 'filters':
                filterList = result;
                filterList.sort();
                updateFilterList();
                break;
            case 'nodeUpdate':
                console.log("Got node update command: " + msg);
                node_cache[result.id] = result;
                updateNode(result);
                break;
            case 'serverRestart':
                serverRestart();
                break;
            default:
                console.log("unknown command from server: " + msg);
                break;
        }
    //} catch (error) {
    //    console.log(error + ": " + error);
    //}
}

function onTrafficOpen(evt) {
    // Once a connection has been made, make a subscription and send a message.
    console.log("onConnect");
    client.subscribe("SPIN/traffic");

    sendCommand("get_filters", {})//, "")
    sendCommand("get_names", {})//, "")
    //show connected status somewhere
    $("#statustext").css("background-color", "#ccffcc").text("Connected");
}

function onTrafficClose(evt) {
    //show disconnected status somewhere
    $("#statustext").css("background-color", "#ffcccc").text("Not connected");
    console.log('Websocket has disappeared');
    console.log(evt.errorMessage)
}

function onTrafficError(evt) {
    //show traffick errors on the console
    console.log('WebSocket traffic error: ' + evt.data);
}

function initTrafficDataView() {
    var data = { 'timestamp': Math.floor(Date.now() / 1000),
                 'total_size': 0, 'total_count': 0,
                 'flows': []}
    handleTrafficMessage(data);
}

// other code goes here
// Update the Graphs (traffic graph and network view)
function handleTrafficMessage(data) {

    var timestamp = data['timestamp']

    // update traffic graph
    var d = new Date(timestamp * 1000);
    traffic_dataset.add({
        x: d,
        y: data['total_size'],
        group: 0
    });
    traffic_dataset.add({
        x: d,
        y: data['total_count'],
        group: 1
    });

    var options = {
        start: new Date((timestamp * 1000) - 600000),
        end: d,
        height: '140px',
        drawPoints: false,
    };
    graph2d_1.setOptions(options);
    var ids = traffic_dataset.getIds();

    //
    // update network view
    //

    // clean out old nodes, and reset color
    // TODO: Websocket heartbeat signal with regular intervals to trigger this part?
    var now = Math.floor(Date.now() / 1000);
    var delete_before = now - 600;
    var unhighlight_before = now - 10;
    var ip;
    for (ip in nodeIds) {
        var nodeId = getNodeId(ip)
        var node = nodes.get(nodeId);
        if (node.address in nodeNames) {
            node.label = nodeNames[node.address];
            nodes.update(node);
        }

        if (node.lastseen < delete_before) {
            deleteNode(node, true);
        } else if (node.lastseen < unhighlight_before && node["color"] == colour_recent) {
            node["color"] = colour_dst;
            nodes.update(node);
        }
    }

    // Add the new flows
    var arr = data['flows'];
    for (var i = 0, len = arr.length; i < len; i++) {
        var f = arr[i];
        // defined in spingraph.js
        //alert("FIND NODE: " + f['from'])
        var from_node = node_cache[f['from']];
        if (!from_node) {
            // some dummy data, ask for data update
            from_node = {};
            from_node.id = f['from'];
            sendCommandDNS('missingNodeInfo', f['from']);
            // what else?
        }
        var to_node = node_cache[f['to']];
        if (!to_node) {
            // some dummy data, ask for data update
            to_node = {};
            to_node.id = f['to'];
            sendCommandDNS('missingNodeInfo', f['to']);
            // what else?
            // TODO send command
        }

        addFlow(timestamp, from_node, to_node, f['count'], f['size']);
    }
}

function handleBlockedMessage(data) {
    var timestamp = data['timestamp']

    var from_node = node_cache[data['from']];
    if (!from_node) {
        // some dummy data, ask for data update
        from_node = {};
        from_node.id = data['from'];
        sendCommandDNS('missingNodeInfo', data['from']);
        // what else?
    }
    var to_node = node_cache[data['to']];
    if (!to_node) {
        // some dummy data, ask for data update
        to_node = {};
        to_node.id = data['to'];
        sendCommandDNS('missingNodeInfo', data['to']);
        // what else?
        // TODO send command
    }
    addBlocked(timestamp, from_node, to_node);
}

function serverRestart() {
    location.reload(true);
}

window.addEventListener("load", init, false);
