var client = new Paho.MQTT.Client("valibox.", 1884, "Web-" + Math.random().toString(16).slice(-5));
//var client = new Paho.MQTT.Client("127.0.0.1", 1884, "clientId");
var last_traffic = 0 // Last received traffic trace
var time_sync = 0; // Time difference (seconds) between server and client
var active = false; // Determines whether we are active or not

function init() {
    initGraphs();

    // set callback handlers
    client.onConnectionLost = onTrafficClose;
    client.onMessageArrived = onMessageArrived;

    // connect the client
    client.connect({onSuccess:onTrafficOpen});

    // Make smooth traffic graph when no data is received
    setInterval(fillEmptiness, 200);
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
    console.log("find element " + element)
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
            case 'traffic':
                //console.log("Got traffic command: " + msg);
                //console.log("handling trafficcommand: " + evt.data);

                // First, update time_sync to account for timing differences
                time_sync = Math.floor(Date.now()/1000 - new Date(result['timestamp']))
                // update the Graphs
                handleTrafficMessage(result);
                break;
            case 'blocked':
                //console.log("Got blocked command: " + msg);
                handleBlockedMessage(result);
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
    //} catch (error) {
    //    console.log(error + ": " + error);
    //}
}

function onTrafficOpen(evt) {
    // Once a connection has been made, make a subscription and send a message..
    console.log("onConnect");
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
    console.log('Websocket has disappeared');
    console.log(evt.errorMessage)
    active = false;
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

// Sometimes, no data is received for some time
// Fill that void by adding 0-value datapoints to the graph
function fillEmptiness() {
    if (active && last_traffic != 0 && Date.now() - last_traffic >= 1000) {
        var data = { 'timestamp': Math.floor(Date.now() / 1000) - time_sync,
                     'total_size': 0, 'total_count': 0,
                     'flows': []}
        handleTrafficMessage(data);
    }
}

// other code goes here
// Update the Graphs (traffic graph and network view)
function handleTrafficMessage(data) {
    // update to report new traffic
    // Do not update if we have not received data yet
    if (!(last_traffic == 0 && data['total_count'] == 0)) {
        last_traffic = Date.now();
    }

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

    // Add the new flows
    var arr = data['flows'];
    for (var i = 0, len = arr.length; i < len; i++) {
        var f = arr[i];
        // defined in spingraph.js
        //alert("FIND NODE: " + f['from'])
        var from_node = f['from'];
        var to_node = f['to'];

        if (from_node != null && to_node != null) {
            addFlow(timestamp + time_sync, from_node, to_node, f['count'], f['size']);
        } else {
            console.log("partial message: " + JSON.stringify(data))
        }
    }
}

function handleBlockedMessage(data) {
    var from_node = data['from'];
    var to_node = data['to'];
    addBlocked(from_node, to_node);
}

function serverRestart() {
    location.reload(true);
}

window.addEventListener("load", init, false);
