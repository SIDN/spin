/*
 * spinsocket.js
 *  https://github.com/SIDN/spin/html/js/spinsocket.js
 *
 * @version 0.0.1
 * @date 2017-02-24
 *
 * SPIN WebSocket code
 * Main functionality: maintain websocket communitation
 *
 */
"use strict";

var traffic_ws;

function init() {
    initGraphs();
    startWebSockets();
}

// determine the correct URI
// TODO: use hostname (valibox.) ?
function createWebSocketUri() {
    var protocolPrefix = (window.location.protocol === 'https:') ? 'wss:' : 'ws:';
    var host = (window.location.host === '') ? '192.168.8.1' : window.location.host
    return protocolPrefix + '//' + host + ":12345/";
}

function startWebSockets() {
    traffic_ws = new WebSocket(createWebSocketUri(), "traffic-data-protocol");
    traffic_ws.onopen = function(evt) {
        onTrafficOpen(evt)
    };
    traffic_ws.onclose = function(evt) {
        onTrafficClose(evt)
    };
    traffic_ws.onmessage = function(evt) {
        onTrafficMessage(evt)
    };
    traffic_ws.onerror = function(evt) {
        onTrafficError(evt)
    };
}

function sendCommand(command, argument) {
    var cmd = {}
    cmd['command'] = command;
    cmd['argument'] = argument;
    //console.log("sending command: '" + command + "' with argument: '" + JSON.stringify(argument) + "'");
    traffic_ws.send(JSON.stringify(cmd));
    // TODO: error-handling when WebSocket is no longer there?
}

function writeToScreen(element, message) {
    var el = document.getElementById(element);
    el.innerHTML = message;
}

function onTrafficMessage(evt) {
    try {
        var message = JSON.parse(evt.data)
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
                break;
            case 'ip2netowner':
                //console.log("issueing ip2netowner command");
                var node = nodes.get(selectedNodeId);
                if (node && node.address == argument) {
                    writeToScreen("netowner", "Network owner: " + result);
                }
                break;
                // this one should be obsolete
                /*case 'arp2dhcpname':
                  //console.log("issueing arp2dhcpname command");
                  if (result && result != "") {
                      var node = nodes.get(getNodeId(argument));
                      node.label = result;
                      nodes.update(node);
                  }
                  break;*/
            case 'traffic':
                //console.log("handling trafficcommand: " + evt.data);
                // update the Graphs
                handleTrafficMessage(result);
                break;
            case 'names':
                nodeNames = result;
                break;
            case 'filters':
                filterList = result;
                filterList.sort();
                updateFilterList();
                break;
            default:
                console.log("unknown command from server: " + evt.data)
                break;
        }
    } catch (error) {
        console.log(error + ": " + evt.data);
    }
}

function onTrafficOpen(evt) {
    //show connected status somewhere
    $("#status").css("background-color", "#ccffcc");
    $(".statustext").css("background-color", "#ccffcc").text("Connected");
    $(".tooltiptext").text("Collecting data.");    
}

function onTrafficClose(evt) {
    //show disconnected status somewhere
    $("#status").css("background-color", "#ffcccc").text("Not connected");
    console.log('Websocket has dissapeared');
}

function onTrafficError(evt) {
    //show traffick errors on the console
    console.log('WebSocket traffic error: ' + evt.data);
}

/* TODO: somehow broke durng splitup - should fix */

function initTrafficDataView() {
    var data = { 'timestamp': Math.floor(Date.now() / 1000), 'flows': []}
    handleTrafficMessage(data);
}

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
        addFlow(timestamp, f['from'], f['to'], f['count'], f['size']);
    }
}

window.addEventListener("load", init, false);
