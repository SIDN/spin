var client = 0;// = new Paho.MQTT.Client("valibox.", 1884, "Web-" + Math.random().toString(16).slice(-5));
var showWsErrorDialog = null;
var showLoginDialog = null;
var username = null;
var password = null;

//var client = new Paho.MQTT.Client("127.0.0.1", 1884, "clientId");
var last_traffic = 0 // Last received traffic trace
var time_sync = 0; // Time difference (seconds) between server and client
var active = false; // Determines whether we are active or not

var datacache = []; // array of all data items to be added on the next iteration.
var nodeinfo = []; // caching array with all node information

// We create and maintain connections to two servers;
// the MQTT server and the web API / RPC server.
// Connection details can in most cases be derived, but they can be
// overridden
var server_data = null;

function setServerData() {
    // Default to values derived from current host/port
    // Parse query parameters to override
    let protocol = window.location.protocol
    let hostname = window.location.hostname
    let port = window.location.port
    let url = new URL(document.location)

    let portstr = "";
    if ((protocol === "http:" && port !== 80) ||
        (protocol === "https:" && port !== 443)
    ) {
        portstr = ":" + window.location.port;
    }

    server_data = {}
    let qp_mqtt_host = url.searchParams.get("mqtt_host")
    if (qp_mqtt_host) {
        server_data.mqtt_host = qp_mqtt_host
    } else {
        server_data.mqtt_host = hostname
    }
    let qp_mqtt_port = url.searchParams.get("mqtt_port")
    if (qp_mqtt_port) {
        server_data.mqtt_port = parseInt(qp_mqtt_port)
    } else {
        server_data.mqtt_port = 1884
    }
    if (window.location.protocol == "https:") {
        server_data.useSSL = true
    } else {
        server_data.useSSL = false
    }

    // TODO: do we want to add query params for api url/port too?
    server_data.api_base_url = protocol + "//" + hostname + portstr

    //alert("API HOST: " + server_data.api_base_url);
}

// Returns the websockets url
function getWsURL() {
    let protocol = "ws://";
    if (server_data.useSSL) {
        protocol = "wss://";
    }
    return protocol + server_data.mqtt_host + ":" + server_data.mqtt_port;
}

// Returns the websockets url as if it is an https url
function getWsURLasHTTP() {
    let protocol = "http://";
    if (server_data.useSSL) {
        protocol = "https://";
    }
    return protocol + server_data.mqtt_host + ":" + server_data.mqtt_port;
}

function init() {
    setServerData();

    client = new Paho.MQTT.Client(server_data.mqtt_host, server_data.mqtt_port, "Web-" + Math.random().toString(16).slice(-5));
    initGraphs();
    connect();
}

function connect() {

    client.onConnectionLost = onTrafficClose;
    client.onMessageArrived = onMessageArrived;

    let options = {
        useSSL: server_data.useSSL,
        onSuccess: onTrafficOpen,
        onFailure: onConnectFailed
    }

    if (username) {
        options.userName = username;
    }
    if (password) {
        options.password = password;
    }

    try {
        client.connect(options);
    } catch (err) {
        // TODO: Ask for username/password here?
        console.error(err);
    }

    // Make smooth traffic graph when no data is received
    setInterval(redrawTrafficGraph, 1000);
}

// called when a message arrives
function onMessageArrived(message) {
    //console.log("SPIN/traffic message:"+message.payloadString);
    onTrafficMessage(message.payloadString);
}

// send a command to the MQTT channel 'SPIN/commands'
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

/*
 * Send an RPC command to the SPIN web API
 * This also checks whether the result has, well, a result, or an
 * error. For now, we simply alert if there is an error, with the
 * full error response.
 * retry_count is used internally to keep track of the number of retries
 * attempted in case of a 502 error
 */
function sendRPCCommand(procedure, params, success_callback, retry_count=0) {
    var xhttp = new XMLHttpRequest();
    let rpc_endpoint = server_data.api_base_url + "/spin_api/jsonrpc";
    xhttp.open("POST", rpc_endpoint, true);
    xhttp.timeout = 5000; // Start with a timeout of 5 seconds
    xhttp.setRequestHeader("Content-Type", "application/json");
    let command = {
        "jsonrpc": "2.0",
        "id": Math.floor(Math.random() * 100000),
        "method": procedure,
        "params": params
    }
    //xhttp.onerror = sendRPCCommandError;
    xhttp.onload = function () {
        //alert("[XX] status: " + xhttp.status);
        if (xhttp.status === 502 && retry_count < 10) {
            // This may be just a timeout or a proxy short read
            // let's simply try again
            sendRPCCommand(procedure, params, success_callback, retry_count+1);
        } else if (xhttp.status !== 200) {
            alert("HTTP error " + xhttp.status + " from SPIN RPC server!");
            console.log(xhttp.response);
        } else {
            console.log("XHTTP response:");
            console.log(xhttp.response);
            let response = JSON.parse(xhttp.response)
            if (response.error) {
                alert("JSON-RPC error from SPIN RPC server: " + xhttp.response);
                console.log(xhttp.response);
            } else {
                console.log("Success response from RPC server: " + xhttp.response);
                if (success_callback && response.result) {
                    success_callback(response.result);
                }
            }
        }
    };
    xhttp.ontimeout = function() {
        // client-side timeout, lets try it again, but increase the retry counter
        if (retry_count < 10) {
            sendRPCCommand(procedure, params, success_callback, retry_count+1);
        } else {
            alert("Too many timeouts on trying to contact SPIN RPC server");
        }
    };
    xhttp.send(JSON.stringify(command));
    console.log("Sent RPC command: " + JSON.stringify(command));
}

function writeToScreen(element, message) {
    var el = document.getElementById(element);
    el.innerHTML = message;
}

var REMOVEME=false;

function onTrafficMessage(msg) {
    if (msg === '') {
        return
    }
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
                //console.log("Got traffic message: " + msg);
                //console.log("handling trafficcommand: " + evt.data);

                // First, update time_sync to account for timing differences
                time_sync = Math.floor(Date.now()/1000 - new Date(result['timestamp']))
                // update the Graphs
                //handleTrafficMessage(result);
                datacache.push(result); // push to cache
                break;
            case 'nodeInfo':
                // This implements the new nodeInfo messages
                // These are publishes on different channels
                //console.log("Got nodeinfo message: " + msg);
                handleNodeInfo(result);
                break;
            case 'blocked':
                //console.log("Got blocked message: " + msg);
                handleBlockedMessage(result);
                break;
            case 'dnsquery':
                //console.log("Got DNS query message: " + msg);
                handleDNSQueryMessage(result);
                break;
            case 'ignore':
                //console.log("Got ignores command: " + msg);
                handle_getignore_response(result)
                //ignoreList = result;
                //ignoreList.sort();
                //updateIgnoreList();
                break;
            case 'block':
                //console.log("Got blocks command: " + msg);
                handle_getblock_response(result)
                //blockList = result;
                //blockList.sort();
                //updateBlockList();
                break;
            case 'allow':
                //console.log("Got alloweds command: " + msg);
                handle_getallow_response(result)
                //allowedList = result;
                //allowedList.sort();
                //updateAllowedList();
                break;
            case 'peakinfo':
                //console.log("Got peak information: " + msg);
                handlePeakInformation(result);
                break;
            case 'nodeUpdate':
                // obeoslete?
                console.log("Got node update command: " + msg);
                // just addNode?
                //updateNode(result);
                break;
            case 'serverRestart':
                serverRestart();
                break;
            case 'nodeMerged':
                // remove the old node, update the new node
                console.log("Got node merged command: " + msg);
                handleNodeMerged(result);
                break;
            case 'nodeDeleted':
                // remove the old node, update the new node
                console.log("Got node deleted command: " + msg);
                handleNodeDeleted(result);
                break;
            default:
                console.log("unknown command from server: " + msg);
                break;
        }
    } catch (error) {
        console.error("Error handling message: '" + msg + "'");
        if (error.stack) {
            console.error("Error: " + error.message);
            console.error("Stacktrace: " + error.stack);
        } else {
            console.error("Error: " + error);
        }
    }
}


function handle_getignore_response(result) {
    ignoreList = result;
    ignoreList.sort();
    updateIgnoreList();
}

function handle_getblock_response(result) {
    blockList = result;
    blockList.sort();
    updateBlockList();
}

function handle_getallow_response(result) {
    allowedList = result;
    allowedList.sort();
    updateAllowedList();
}

function onTrafficOpen(evt) {
    // Once a connection has been made, make a subscription and send a message..
    console.log("Connected");
    client.subscribe("SPIN/traffic/#");

    sendRPCCommand("list_iplist", { "list": "ignore" }, handle_getignore_response);
    sendRPCCommand("list_iplist", { "list": "block" }, handle_getblock_response);
    sendRPCCommand("list_iplist", { "list": "allow" }, handle_getallow_response);
    //show connected status somewhere
    $("#statustext").css("background-color", "#ccffcc").text("Connected");
    active = true;
}

function onConnectFailed(evt) {
    console.error("Error connecting to MQTT websockets server: " + JSON.stringify(evt));
    // If the server is using wss with a self-signed certificate, the browser
    // may need explicit permission to access it; browsers tend to reject
    // rather than ask for permission if it's a 'secondary' connection like
    // this. So unfortunately we'll have to get the user to jump through a
    // hoop a little bit. Unfortunately 2: the error itself isn't clear
    // on what the problem is.
    if (evt.errorCode == 7) {
        //alert(JSON.stringify(server_data));
        // open a dialog
        if (showWsErrorDialog) {
            //alert(getWsURL());
            showWsErrorDialog(getWsURL(), getWsURLasHTTP());
        }
    } else if (evt.errorCode == 6) {
        if (showLoginDialog) {
            showLoginDialog();
        }
    }
}

function setWsErrorDialog(callback) {
    showWsErrorDialog = callback;
}

function setLoginDialog(callback) {
    showLoginDialog = callback;
}

function setLoginData(user, pass) {
    username = user;
    pass = pass;
    // automatically try reconnect
    connect();
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
            // New version of protocol: if from_node or to_node is numeric, load from cache
            var from_node = getNodeInfo(f['from']);
            var to_node = getNodeInfo(f['to']);
            //alert(JSON.stringify(f))

            if (from_node != null && to_node != null && from_node && to_node) {
                addFlow(timestamp + time_sync, from_node, to_node, f['count'], f['size'], f['to_port']);
            } else {
                console.error("partial message: " + JSON.stringify(data))
            }
        }
    }
    traffic_dataset.add(elements.concat(elements_cnt));
    //alert(traffic_dataset.add(elements.concat(elements_cnt)));
    // console.log("Graph updated")
    //traffic_dataset.getIds();

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
    var from_node = getNodeInfo(data['from']);
    var to_node = getNodeInfo(data['to']);
    addBlocked(from_node, to_node);
}

function handleDNSQueryMessage(data) {
    var from_node = getNodeInfo(data['from']);
    var dns_node = getNodeInfo(data['queriednode']);
    addDNSQuery(from_node, dns_node);
}

function handleNodeMerged(data) {
    // Do we need to do something with the merged-to?
    // We probably only need to remove our refs to the old one
    var deletedNodeId = data['id']
    var node = getNodeInfo(deletedNodeId)
    if (node !== null) {
        deleteNode(node);
        nodeinfo[data["id"]] = null;
    }
}

function handleNodeDeleted(data) {
    var deletedNodeId = data['id']
    var node = getNodeInfo(deletedNodeId)
    if (node !== null) {
        deleteNode(deletedNodeId);
        nodeinfo[data["id"]] = null;
    }
}

// Handles nodeInfo command
function handleNodeInfo(data) {
    nodeinfo[data["id"]] = data;
}

function getNodeInfo(id) {
    if (Number.isInteger(id)) {
        if (nodeinfo[id]) {
            return nodeinfo[id];
        } else {
            console.error("no nodeInfo for node " + id)
            return false;
        }
    } else {
        // probably old-style protocol
        return id;
    }
}

function serverRestart() {
    location.reload(true);
}

window.addEventListener("load", init, false);
