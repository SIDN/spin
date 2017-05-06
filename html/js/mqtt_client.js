var client = new Paho.MQTT.Client("192.168.8.1", 1884, "clientId");

function init() {
    initGraphs();

    // set callback handlers
    client.onConnectionLost = onTrafficClose;
    client.onMessageArrived = onMessageArrived;

    // connect the client
    client.connect({onSuccess:onTrafficOpen});
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
                console.log("Got traffic command: " + msg);
                //console.log("handling trafficcommand: " + evt.data);
                // update the Graphs
                handleTrafficMessage(result);
                break;
            case 'blocked':
                //console.log("Got blocked command: " + msg);
                handleBlockedMessage(result);
                break;
            case 'filters':
                filterList = result;
                filterList.sort();
                updateFilterList();
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

    // Add the new flows
    var arr = data['flows'];
    for (var i = 0, len = arr.length; i < len; i++) {
        var f = arr[i];
        // defined in spingraph.js
        //alert("FIND NODE: " + f['from'])
        var from_node = f['from'];
        var to_node = f['to'];

        if (from_node != null && to_node != null) {
            addFlow(timestamp, from_node, to_node, f['count'], f['size']);
        } else {
            console.log("partial message: " + JSON.stringify(data))
        }
    }
}

function handleBlockedMessage(data) {
    var timestamp = data['timestamp']

    var from_node = data['from'];
    var to_node = data['to'];
    addBlocked(timestamp, from_node, to_node);
}

function serverRestart() {
    location.reload(true);
}

window.addEventListener("load", init, false);
