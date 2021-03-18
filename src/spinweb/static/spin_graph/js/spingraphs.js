/*
 * spin.js
 *  https://github.com/SIDN/spin/html/js/spin.js
 *
 * @version 0.0.1
 * @date 2017-02-24
 *
 * SPIN Graphical module
 * Main functionality: Maintain the dashboard, in particular the VisJS graphs
 *
 */
"use strict";

var traffic_dataset = new vis.DataSet([]);
var graph2d_1;
var graph_peak_packets;
var graph_peak_bytes;
var selectedNodeId;
var selectedEdgeId;
// list of ignores
var ignoreList = [];
var blockList = [];
var allowedList = [];
// feed this data from websocket command
var zoom_locked = false;

var colour_src = "#dddddd";
var colour_dst = "lightblue";
var colour_recent = "#bbffbb";
var colour_edge = "#9999ff";
var colour_blocked = "#ff0000";
var colour_dns = "#ffab44";

var nodes;

// these are used in the filterlist dialog
var _selectRange = false,
    _deselectQueue = [];

function enableZoomLock() {
    updateZoomLock(true);
}

function toggleZoomLock() {
    updateZoomLock(!zoom_locked);
}

function updateZoomLock(newBool) {
    zoom_locked = newBool;
    if (zoom_locked) {
        $("#autozoom-button").button("option", {
            "icons": {
                "primary": "ui-icon-locked"
            },
            "label": "Unlock view"
        });
    } else {
        $("#autozoom-button").button("option", {
            "icons": {
                "primary": "ui-icon-unlocked"
            },
            "label": "Lock view"
        });
    }
}

// code to add filters
function sendAddIgnoreCommand(nodeId) {
    var node = nodes.get(nodeId);
    // pre-emptively update the local list so we don't need to do or
    // wait for another sync
    if ("ips" in node) {
        for (var i = 0; i < node.ips.length; i++) {
            ignoreList.push(node.ips[i]);
        }
    }

    sendRPCCommand("add_iplist_node", { "list": "ignore", "node": nodeId });
    deleteNodeAndConnectedNodes(node);
}

function updateIgnoreList() {
    $("#filter-list").empty();
    for (var i = 0; i < ignoreList.length; i++) {
        var li = $("<li class='ui-widget-content'></li> ").text(ignoreList[i]);
        $("#filter-list").append(li);
    }
}

function updateBlockList() {
    $("#block-list").empty();
    for (var i = 0; i < blockList.length; i++) {
        var li = $("<li class='ui-widget-content'></li> ").text(blockList[i]);
        $("#block-list").append(li);
    }
    updateBlockedNodes();
    updateBlockedButton();
}

function updateAllowedList() {
    $("#allowed-list").empty();
    for (var i = 0; i < allowedList.length; i++) {
        var li = $("<li class='ui-widget-content'></li> ").text(allowedList[i]);
        $("#allowed-list").append(li);
    }
    updateAllowedNodes();
    updateAllowedButton();
}


function initGraphs() {
    $("#new-filter-dialog").hide();
    $("#rename-dialog").hide();
    $("#autozoom-button").button({
        "icons": {
            "primary": "ui-icon-unlocked"
        },
        "label": "Lock view"
    }).click(toggleZoomLock);

    // create the node information dialog
    $("#nodeinfo").dialog({
        autoOpen: false,
        position: {
            my: "left top",
            at: "left top",
            of: "#mynetwork"
        },
        close: nodeInfoClosed,
    });

    // create the flow information dialog
    $("#flowinfo").dialog({
        autoOpen: false,
        position: {
            my: "left top",
            at: "left top",
            of: "#mynetwork"
        },
        close: flowInfoClosed,
    });

    // create the ignore node dialog
    $(function() {
        var dialog;

        dialog = $("#new-filter-dialog").dialog({
            autoOpen: false,
            resizable: false,
            height: "auto",
            width: 400,
            modal: true,
            buttons: {
                "Ignore node": function() {
                    sendAddIgnoreCommand(selectedNodeId);
                    $(this).dialog("close");
                },
                Cancel: function() {
                    $(this).dialog("close");
                }
            }
        });
        $("#add-filter-button").button().on("click", function() {
            dialog.dialog("open");
        });
    });

    // create the rename dialog
    $(function() {
        var name = $("#rename-field");
        var dialog;
        var allFields = $([]).add(name);

        function renameNode() {
            var argument = {};
            var node = nodes.get(selectedNodeId);
            var newname = name.val();
            argument['node_id'] = selectedNodeId;
            argument['name'] = newname;
            sendRPCCommand("set_device_name", { 'node': node.id, 'name': newname });

            node.label = newname;
            node.name = newname;
            nodes.update(node);
        }

        dialog = $("#rename-dialog").dialog({
            autoOpen: false,
            width: 380,
            modal: true,
            buttons: {
                "Rename node": submitted,
                Cancel: function() {
                    dialog.dialog("close");
                }
            },
            close: function() {
                form[0].reset();
                allFields.removeClass("ui-state-error");
            }
        });

        function submitted() {
            renameNode();
            dialog.dialog("close");
        }

        var form = dialog.find("form").on("submit", function(event) {
            event.preventDefault();
            submitted();
        });

        $("#rename-node-button").button().on("click", function() {
            dialog.dialog("open");
        });
    });

    // create the filterlist dialog
    // todo: refactor next 3 into 1 call
    $(function() {
        $("#filter-list").selectable({
            selecting: function(event, ui) {
                if (event.detail == 0) {
                    _selectRange = true;
                    return true;
                }
                if ($(ui.selecting).hasClass('ui-selected')) {
                    _deselectQueue.push(ui.selecting);
                }
            },
            unselecting: function(event, ui) {
                $(ui.unselecting).addClass('ui-selected');
            },
            stop: function() {
                if (!_selectRange) {
                    $.each(_deselectQueue, function(ix, de) {
                        $(de)
                            .removeClass('ui-selecting')
                            .removeClass('ui-selected');
                    });
                }
                _selectRange = false;
                _deselectQueue = [];

                // enable or disable the remove filters button depending
                // on whether any elements have been selected
                var selected = [];
                $(".ui-selected", this).each(function() {
                    selected.push(index);
                    var index = $("#filter-list li").index(this);
                });
                if (selected.length > 0) {
                    $(".ui-dialog-buttonpane button:contains('Remove Ignores')").button("enable");
                } else {
                    $(".ui-dialog-buttonpane button:contains('Remove Ignores')").button("disable");
                }
            }
        });
    });

    $(function() {
        $("#block-list").selectable({
            selecting: function(event, ui) {
                if (event.detail == 0) {
                    _selectRange = true;
                    return true;
                }
                if ($(ui.selecting).hasClass('ui-selected')) {
                    _deselectQueue.push(ui.selecting);
                }
            },
            unselecting: function(event, ui) {
                $(ui.unselecting).addClass('ui-selected');
            },
            stop: function() {
                if (!_selectRange) {
                    $.each(_deselectQueue, function(ix, de) {
                        $(de)
                            .removeClass('ui-selecting')
                            .removeClass('ui-selected');
                    });
                }
                _selectRange = false;
                _deselectQueue = [];

                // enable or disable the remove blocks button depending
                // on whether any elements have been selected
                var selected = [];
                $(".ui-selected", this).each(function() {
                    selected.push(index);
                    var index = $("#block-list li").index(this);
                });
                if (selected.length > 0) {
                    $(".ui-dialog-buttonpane button:contains('Remove Blocks')").button("enable");
                } else {
                    $(".ui-dialog-buttonpane button:contains('Remove Blocks')").button("disable");
                }
            }
        });
    });

    $(function() {
        $("#allowed-list").selectable({
            selecting: function(event, ui) {
                if (event.detail == 0) {
                    _selectRange = true;
                    return true;
                }
                if ($(ui.selecting).hasClass('ui-selected')) {
                    _deselectQueue.push(ui.selecting);
                }
            },
            unselecting: function(event, ui) {
                $(ui.unselecting).addClass('ui-selected');
            },
            stop: function() {
                if (!_selectRange) {
                    $.each(_deselectQueue, function(ix, de) {
                        $(de)
                            .removeClass('ui-selecting')
                            .removeClass('ui-selected');
                    });
                }
                _selectRange = false;
                _deselectQueue = [];

                // enable or disable the remove alloweds button depending
                // on whether any elements have been selected
                var selected = [];
                $(".ui-selected", this).each(function() {
                    selected.push(index);
                    var index = $("#allowed-list li").index(this);
                });
                if (selected.length > 0) {
                    $(".ui-dialog-buttonpane button:contains('Remove Allowed')").button("enable");
                } else {
                    $(".ui-dialog-buttonpane button:contains('Remove Allowed')").button("disable");
                }
            }
        });
    });


    $(function() {
        var dialog;
        var block_dialog;
        var allowed_dialog;
        var selected;

        dialog = $("#filter-list-dialog").dialog({
            autoOpen: false,
            autoResize: true,
            resizable: true,
            modal: false,
            minWidth: 360,
            position: {
                my: "right top",
                at: "right top",
                of: "#mynetwork"
            },
            buttons: {
                "Remove Ignores": function() {
                    $("#filter-list .ui-selected", this).each(function() {
                        // The inner text contains the name of the filter
                        var address;
                        if (this.innerText) {
                            sendRPCCommand("remove_iplist_ip", { "list": "ignore", "ip": this.innerText });
                        } else if (this.innerHTML) {
                            sendRPCCommand("remove_iplist_ip", { "list": "ignore", "ip": this.innerHTML });
                        }
                        $(".ui-dialog-buttonpane button:contains('Remove Ignores')").button("disable");
                    });
                },
                Reset: function() {
                    sendRPCCommand("reset_iplist_ignore");
                },
                Close: function() {
                    dialog.dialog("close");
                }
            },
            close: function() {}
        });

        $(".ui-dialog-buttonpane button:contains('Remove Ignores')").button("disable");
        $("#filter-list-button").button().on("click", function() {
            dialog.dialog("open");
        });


        block_dialog = $("#block-list-dialog").dialog({
            autoOpen: false,
            autoResize: true,
            resizable: true,
            modal: false,
            minWidth: 360,
            position: {
                my: "right top",
                at: "right top",
                of: "#mynetwork"
            },
            buttons: {
                "Remove Blocks": function() {
                    $("#block-list .ui-selected", this).each(function() {
                        // The inner text contains the name of the block
                        var address;
                        if (this.innerText) {
                            sendRPCCommand("remove_iplist_ip", { "list": "block", "ip": this.innerText });
                        } else if (this.innerHTML) {
                            sendRPCCommand("remove_iplist_ip", { "list": "block", "ip": this.innerHTML });
                        }
                        $(".ui-dialog-buttonpane button:contains('Remove Blocks')").button("disable");
                    });
                },
                Close: function() {
                    block_dialog.dialog("close");
                }
            },
            close: function() {}
        });

        $(".ui-dialog-buttonpane button:contains('Remove Blocks')").button("disable");
        $("#block-list-button").button().on("click", function() {
            block_dialog.dialog("open");
        });


        allowed_dialog = $("#allowed-list-dialog").dialog({
            autoOpen: false,
            autoResize: true,
            resizable: true,
            modal: false,
            minWidth: 360,
            position: {
                my: "right top",
                at: "right top",
                of: "#mynetwork"
            },
            buttons: {
                "Remove Allowed": function() {
                    $("#allowed-list .ui-selected", this).each(function() {
                        // The inner text contains the name of the block
                        var address;
                        if (this.innerText) {
                            sendRPCCommand("remove_iplist_ip", { "list": "allow", "ip": this.innerText });
                        } else if (this.innerHTML) {
                            sendRPCCommand("remove_iplist_ip", { "list": "allow", "ip": this.innerHTML });
                        }
                        $(".ui-dialog-buttonpane button:contains('Remove Allowed')").button("disable");
                    });
                },
                Close: function() {
                    allowed_dialog.dialog("close");
                }
            },
            close: function() {}
        });

        $(".ui-dialog-buttonpane button:contains('Remove Allowed')").button("disable");
        $("#allowed-list-button").button().on("click", function() {
            allowed_dialog.dialog("open");
        });
    });

    $("#block-node-button").button().on("click", function (evt) {
        var node = nodes.get(selectedNodeId);
        // hmm. misschien we should actually remove the node and
        // let the next occurrence take care of presentation?
        if (node.is_blocked) {
            sendRPCCommand("remove_iplist_node", { "list": "block", "node": selectedNodeId });
            node.is_blocked = false;
        } else {
            sendRPCCommand("add_iplist_node", { "list": "block", "node": selectedNodeId });
            node.is_blocked = true;
        }
        nodes.update(node);
        updateBlockedButton();
    });

    $("#allow-node-button").button().on("click", function (evt) {
        var node = nodes.get(selectedNodeId);
        // hmm. misschien we should actually remove the node and
        // let the next occurrence take care of presentation?
        if (node.is_excepted) {
            sendRPCCommand("remove_iplist_node", { "list": "allow", "node": selectedNodeId });
            node.is_excepted = false;
        } else {
            sendRPCCommand("add_iplist_node", { "list": "allow", "node": selectedNodeId });
            node.is_excepted = true;
        }
        nodes.update(node);
        updateAllowedButton();
    });

    $("#pcap-node-button").button().on("click", function (evt) {
        var node = nodes.get(selectedNodeId);
        var name = node.mac;

        if (!name) {
            alert("Sorry, for now we can only dump pcap for devices, not remote nodes");
            return;
        }
        // Spin_webui.lua must be started and lua-minittp installed for the capture
        // to work
        let portstr = "";
        if ((window.location.protocol === "http:" && window.location.port !== 80) ||
            (window.location.protocol === "https:" && window.location.port !== 443)
        ) {
            portstr = ":" + window.location.port;
        }
        var url = window.location.protocol + "//" + window.location.hostname + portstr +
        "/spin_api/capture?device="+name;
        var w = window.open(url, name, "width=444,  height=534, scrollbars=yes");
    });

    $("#block-flow-button").button().on("click", function (evt) {
        let edge = edges.get(selectedEdgeId);
        let block;
        if (isBlockedEdge(selectedEdgeId)) {
            block = 0;
            edge.blocked = false;
        } else {
            block = 1;
            edge.blocked = true;
        }
        let params = {
            "node1": edge.from,
            "node2": edge.to,
            "block": block
        };
        edges.update(edge)
        updateEdgeInfo(selectedEdgeId);
        //alert(JSON.stringify(params));
        sendRPCCommand("set_flow_block", params);
    });

    showGraph(traffic_dataset);
    showNetwork();
    initTrafficDataView();

    // clean up every X seconds
    setInterval(cleanNetwork, 5000);
    
    // Refresh peak information every 30 seconds
    setInterval(getPeakInformation, 30000);
}

//
// Traffic-graph code
//
function showGraph(dataset) {
    var container = document.getElementById('visualization');

    //
    // Group options
    //
    var names = ['Traffic size', 'Packet count'];
    var groups = new vis.DataSet();
    groups.add({
        id: 0,
        content: names[0],
        options: {
            shaded: {
                orientation: 'bottom' // top, bottom
            }
        }
    });

    groups.add({
        id: 1,
        content: names[1],
        options: {
            shaded: {
                orientation: 'bottom' // top, bottom
            },
            yAxisOrientation: 'right'
        }
    });

    // Graph options
    var options = {
        start: new Date(Date.now()),
        end: new Date(Date.now() + 600000),
        height: '140px',
        drawPoints: false,
        zoomable: false,
        moveable: false,
        showCurrentTime: true,
        //clickToUse: true
    };

    graph2d_1 = new vis.Graph2d(container, dataset, groups, options);
}

//
// Network-view code
//

var shadowState, nodesArray, nodes, edgesArray, edges, network, curNodeId, curEdgeId;


function showNetwork() {
    // mapping from ip to nodeId
    shadowState = true;

    // start counting with one (is this internally handled?)
    curNodeId = 1;
    curEdgeId = 1;

    // create an array with nodes
    nodesArray = [];
    nodes = new vis.DataSet(nodesArray);

    // create an array with edges
    edgesArray = [];
    edges = new vis.DataSet(edgesArray);

    // create a network
    var container = document.getElementById('mynetwork');
    var data = {
        nodes: nodes,
        edges: edges
    };
    var options = {
        autoResize: true,
        //clickToUse: true,
        physics: {
            //solver: 'repulsion',
            enabled:true,
            barnesHut: {
/*
                gravitationalConstant: -2000,
                centralGravity: 0.3,
                springLength: 95,
                springConstant: 0.04,
                damping: 0.09,
                avoidOverlap: 0.5
*/
            },
            stabilization: {
                enabled: true,
                iterations: 5,
                updateInterval: 10,
            }
        },
        nodes: {
            shadow:shadowState
        },
        edges: {
            shadow:shadowState,
            arrows:'to',
            smooth:true
        },
        interaction: {
            selectConnectedEdges: false
        }
    };
    network = new vis.Network(container, data, options);
    network.on("selectNode", nodeSelected);
    network.on("deselectNode", nodeDeselected);
    network.on("selectEdge", edgeSelected);
    network.on("deselectEdge", edgeDeselected);
    network.on("zoom", enableZoomLock);
}

function updateNodeInfo(nodeId) {
    var node = nodes.get(nodeId);
    var sizeblabel = 'B';
    var sizeb = 0;
    if (node.size > 0) {
        sizeblabel = ['B','KiB', 'MiB', 'GiB', 'TiB'][Math.floor(Math.log2(node.size)/10)]
        sizeb = Math.floor(node.size / ([1,1024, 1024**2, 1024**3, 1024**4][Math.floor(Math.log2(node.size)/10)]))
    } 

    writeToScreen("trafficcount", "<b>Packets seen</b>: " + node.count);
    writeToScreen("trafficsize", "<b>Traffic size</b>: " + sizeb + " " + sizeblabel);
    writeToScreen("ipaddress", "");
    var d = new Date(node.lastseen * 1000);
    writeToScreen("lastseen", "<b>Last seen</b>: " + d.toLocaleDateString() + " " + d.toLocaleTimeString() + " (" + node.lastseen + ")");

    writeToScreen("nodeid", "<b>Node</b>: " + nodeId);
    if (node.mac) {
        writeToScreen("mac", "<b>HW Addr</b>: " + node.mac);
    } else {
        writeToScreen("mac", "");
    }
    if (node.ips) {
        writeToScreen("ipaddress", "<b>IP</b>: " + node.ips.join("</br>"));
    } else {
        writeToScreen("ipaddress", "<b>IP</b>: ");
    }
    if (node.domains) {
        writeToScreen("reversedns", "<b>DNS</b>: " + node.domains.join("</br>"));
    } else {
        writeToScreen("reversedns", "<b>DNS</b>: ");
    }

    return node;
}

function updateEdgeInfo(edgeId) {
    let edge = edges.get(edgeId);
    let fromlabel = nodes.get(edge.from).label
    let tolabel = nodes.get(edge.to).label
    writeToScreen("fromnodeid", "<b>From</b>: " + fromlabel);
    writeToScreen("tonodeid", "<b>To</b>: " + tolabel);
    var label = isBlockedEdge(edgeId) ? "Unblock flow" : "Block flow";
    $("#block-flow-button").button("option", {
        "label": label
    });
}

function edgeSelected(event) {
    selectedEdgeId = event.edges[0];
    updateEdgeInfo(selectedEdgeId);
    $("#flowinfo").dialog('open');
}

function edgeDeselected(event) {
    selectedEdgeId = 0;
    $("#flowinfo").dialog('close');
}

function nodeDeselected(event) {
    selectedNodeId = 0;
    $("#nodeinfo").dialog('close');
}

function updateNodeData(data) {
    handleNodeInfo(data);
}

function nodeSelected(event) {
    var nodeId = event.nodes[0];
    if (typeof(nodeId) == 'number' && selectedNodeId != nodeId) {
        var node = updateNodeInfo(nodeId);
        selectedNodeId = nodeId;

        updateBlockedButton();
        updateAllowedButton();

        /* Arrange peak detection graph */
        if (graph_peak_packets != null){
            graph_peak_packets.destroy();
            graph_peak_packets = null;
        }
        if (graph_peak_bytes != null){
            graph_peak_bytes.destroy();
            graph_peak_bytes = null;
        }
        
        $("#nodeinfo-peakdet").hide(); // Always hide peak detection, show when active.
        if (node.mac) {
            // Obtain peak information from server.
            // Uses variable selectedNodeId as set above.
            getPeakInformation();
        }

        //writeToScreen("netowner", "Network owner: &lt;searching&gt;");
        $("#nodeinfo").dialog('option', 'title', node.label);
        $("#flowinfo").dialog('close');
        $("#nodeinfo").dialog('open');

    }

    // Just to be sure we have the latest data, let's do an RPC request
    // to update it
    sendRPCCommand("get_device_data", { "node": nodeId }, updateNodeData);
}

function updateBlockedButton() {
    var node = nodes.get(selectedNodeId);
    if (node == null) return;
    var label = node.is_blocked ? "Unblock node" : "Block node";
    $("#block-node-button").button("option", {
        "label": label
    });
}

function updateAllowedButton() {
    var node = nodes.get(selectedNodeId);
    if (node == null) return;
    var label = node.is_excepted ? "Stop allowing node" : "Allow node";
    $("#allow-node-button").button("option", {
        "label": label
    });
}


/* needed?
    function resetAllNodes() {
        nodes.clear();
        edges.clear();
        nodes.add(nodesArray);
        edges.add(edgesArray);
    }

    function resetAllNodesStabilize() {
        resetAllNodes();
        network.stabilize();
    }

    function resetAll() {
        if (network !== null) {
            network.destroy();
            network = null;
        }
        showNetwork();
    }

    function setTheData() {
        nodes = new vis.DataSet(nodesArray);
        edges = new vis.DataSet(edgesArray);
        network.setData({nodes:nodes, edges:edges})
    }

    function haveNode(ip) {
        return (ip in nodeIds);
    }

    function alertWithObject(m, o) {
        str = m + "\n";
        for(var propertyName in o) {
           // propertyName is what you want
           // you can get the value like this: myObject[propertyName]
           str += propertyName + ": " + o[propertyName] + "\n";
        }
        alert(str);
    }

*/

// Used in AddFlow()
function addNode(timestamp, node, scale, count, size, lwith, type) {
    // why does this happen
    if (!node) { return; }
    // find the 'best' info we can display in the main view
    var label = "<working>";
    var colour = colour_recent;
    var ips = node.ips ? node.ips : [];
    var domains = node.domains ? node.domains : [];
    node.is_blocked = node.is_blocked || isBlocked(node);
    var blocked = type == "blocked";
    var dnsquery = type == "dnsquery";

    if (node.name) {
        label = node.name;
    } else if (node.mac) {
        label = node.mac;
    } else if (domains.length > 0) {
        label = node.domains[0];
    } else if (ips.length > 0) {
        if (node.ips[0].match(/^224.0.0/)) {
            // Multicast ipv4
            label = "Multicast IPv4";
        }
        else if (node.ips[0].match(/^ff/)) {
            // Multicast ipv6
            label = "Multicast IPv6";
        } else {
            // Generic ip
            label = node.ips[0];
        }
    }

    if (node.mac) {
        colour = colour_src;
    } else {
        if (blocked) {
            // Node observes blocked traffic
            colour = colour_blocked;
        } else if (dnsquery) {
            // if type is dnsquery, and the node was either already dnsquery or is new,
            // set color to dnsquery. Otherwise, mark it as 'recent' again.
            if (nodes.get(node.id) && nodes.get(node.id).color_set != colour_dns) {
                colour = colour_recent;
            } else {
                colour = colour_dns;
            }
        } else {
            // In all other cases
            colour = colour_recent;
        }
    }

    var border_colour = "black";
    if (node.is_blocked) {
        border_colour = "red";
    }
    if (node.is_excepted) {
        border_colour = "green";
    }

    //alert("add node: " + node)
    var enode = nodes.get(node.id);
    if (!enode) {
        // there is currently a small race condition with nodes from queries
        // and nodes from traffic; so add one extra check to see it we really
        // don't have the domain just yet
        for (let ni = 0; ni < nodesArray.length; ni++) {
            let curn = nodesArray[ni]
            for (let di = 0; di < curn.domains.length; di++) {
                for (let dj = 0; dj < node.domains.length; dj++) {
                    //alert("try " + dj + " and " + 
                    if (node.domains[dj] == curn.domains[di]) {
                        enode = curn
                    }
                }
            }
        }
    }
    if (enode) {
        // update is
        enode.label = label;
        enode.ips = ips;
        enode.domains = domains;
        enode.color_set = colour;
        enode.color = { 'background': colour, 'border': border_colour };
        enode.blocked = blocked;
        enode.lastseen = timestamp;
        enode.is_blocked = node.is_blocked;
        enode.is_excepted = node.is_excepted;
        enode.size += size;
        enode.count += count;
        nodes.update(enode);
    } else {
        // it's new
        nodes.add({
            id: node.id,
            mac: node.mac,
            ips: node.ips ? node.ips : [],
            domains: node.domains ? node.domains : [],
            label: label,
            color_set: colour,
            color: { 'background': colour, 'border': border_colour },
            value: size,
            count: count,
            size: size,
            lastseen: timestamp,
            // blocked means this node was involved in blocked traffic
            blocked: blocked,
            is_blocked: node.is_blocked,
            is_excepted: node.is_excepted,
            scaling: {
                customScalingFunction: function (min, max, total, value) {
                    return value / max;
                },
                //min: 1,
                //max: 150,
            },
        });
    }
    // If node is selected, update labels
    if (node.id == selectedNodeId) {
        updateNodeInfo(node.id);
    }
}


function getServiceForPort(port) {
    // Remember to include port_services.js for this to work
    let service = get_port_service(port);
    if (service) {
        return service;
    } else {
        return port.toString();
    }
}

// Used in AddFlow()
function addEdge(from, to, colour, blocked, dest_port) {
    var existing = edges.get({
        filter: function(item) {
            return (item.from == from.id && item.to == to.id);
        }
    });
    if (existing.length == 0) {
        let dest_ports = {}
        let dest_ports_str = null;
        if (dest_port) {
            dest_ports[dest_port] = true;
            dest_ports_str = getServiceForPort(dest_port);
        }
        edges.add({
            id: curEdgeId,
            from: from.id,
            to: to.id,
            color: {color: colour},
            blocked: blocked,
            label: dest_ports_str,
            dest_ports: dest_ports
        });
        curEdgeId += 1;
    } else {
        let edge = existing[0];
        if (edge.color.color != colour) {
            // If color changed, update it!
            edge.color = {color: colour};
            if (blocked !== undefined) {
                edge.blocked = blocked;
            }
            edges.update(edge);
        }
        if (dest_port) {
            let dest_ports = {};
            if (edge.dest_ports) {
                dest_ports = edge.dest_ports;
            }
            if (!dest_ports[dest_port]) {
                dest_ports[dest_port] = true;
                let dest_ports_str = "";
                for (let key in dest_ports) {
                    if (dest_ports_str != "") {
                        dest_ports_str += ", ";
                    }
                    dest_ports_str += getServiceForPort(key);
                }
                edge.dest_ports = dest_ports;
                edge.label = dest_ports_str;
                edges.update(edge);
            }
        }
    }
}

// Delete the given node (note: not nodeId) and all nodes
// that are connected only to this node
function deleteNodeAndConnectedNodes(node) {
    // find all nodes to delete; that is this node and all nodes
    // connected to it that have no other connections
    network.stopSimulation();
    var connectedNodes = getConnectedNodes(node.id);
    var toDelete = [];
    for (var i=0; i < connectedNodes.length; i++) {
        var otherNodeId = connectedNodes[i];
        var otherConnections = getConnectedNodes(otherNodeId);
        if (otherConnections.length == 1) {
            deleteNode(nodes.get(otherNodeId), false);
        }
    }
    deleteNode(node, true);
    network.startSimulation();
}

// Remove a node from the screen
// If deleteEdges is true, also remove all edges connected to this node
function deleteNode(node, deleteNodeEdges) {
    if (deleteNodeEdges) {
        deleteEdges(node.id);
    }
    nodes.remove(node.id);
}

// Returns a list of all nodeIds that have an edge to the given nodeId
function getConnectedNodes(nodeId) {
    var result = [];
    var cedges = edges.get({
        filter: function(item) {
            return (item.from == nodeId);
        }
    });
    for (var i=0; i < cedges.length; i++) {
        var edge = cedges[i];
        if (!contains(result, edge.to)) {
            result.push(edge.to);
        }
    }
    cedges = edges.get({
        filter: function(item) {
            return (item.to == nodeId);
        }
    });
    for (var i=0; i < cedges.length; i++) {
        var edge = cedges[i];
        if (!contains(result, edge.from)) {
            result.push(edge.from);
        }
    }
    return result;
}


// Used in spinsocket.js
function deleteEdges(nodeId) {
    var toDelete = edges.get({
        filter: function(item) {
            return (item.from == nodeId || item.to == nodeId);
        }
    });
    for (var i = 0; i < toDelete.length; i++) {
        var edge = toDelete[i];
        var e = edges.get(edge);
        edges.remove(edge.id);
    }
}

function contains(l, e) {
    for (var i=l.length; i>=0; i--) {
        if (l[i] == e) {
            return true;
        }
    }
    return false;
}

// Used in spinsocket.js
function addFlow(timestamp, from, to, count, size, dest_port) {
    for (var i = 0; i < to.ips.length; i++) {
      var ip = to.ips[i];
      if (contains(ignoreList, ip)) {
        return;
      }
    }
    for (var i = 0; i < from.ips.length; i++) {
      var ip = from.ips[i];
      if (contains(ignoreList, ip)) {
        return;
      }
    }
    addNode(timestamp, from, false, count, size, "to " + to, "source");
    addNode(timestamp, to, true, count, size, "from " + from, "traffic");
    addEdge(from, to, colour_edge, false, dest_port);
    if (!zoom_locked) {
        network.fit({
            duration: 0
        });
    }
}

function addBlocked(from, to) {
    if (contains(ignoreList, from) || contains(ignoreList, to)) {
        return;
    }
    // Calculate the 'lastseen' timestamp from our own clock
    var timestamp = Math.floor(Date.now() / 1000);
    addNode(timestamp, from, false, 1, 1, "to " + to, "source");
    addNode(timestamp, to, false, 1, 1, "from " + from, "blocked");
    addEdge(from, to, colour_blocked, true);
    if (!zoom_locked) {
        network.fit({
            duration: 0
        });
    }
}

function addDNSQuery(from, dns) {
    if (contains(ignoreList, from) || contains(ignoreList, dns)) {
        return;
    }

    // from["lastseen"] is the existing lastseen value, so we update
    // the from node with the lastseen value of the dns request too
    var timestamp = dns["lastseen"];
    addNode(timestamp, from, false, 0, 0, "to " + dns, "source");
    addNode(timestamp, dns, false, 0, 0, "dns " + from, "dnsquery");
    addEdge(from, dns, colour_dns);
    if (!zoom_locked) {
        network.fit({
            duration: 0
        });
    }
}

/*
 Function to check whether a specific node is blocked.
 Requires the list 'ips' to be present
 */
function isBlocked(node) {
    if ("ips" in node) {
        for (var j = 0; j < node.ips.length; j++) {
            return contains(blockList, node.ips[j]);
        }
    }
    return false
}

// Returns true if the edge is blocked
// Note that this is more than just the 'blocked' status of the edge;
// both the source and the target must not be blocked themselves
// ('blocked' status is set when blocked traffic is seen, but this
// may be caused by either node being fully blocked, rather than the
// specific edge)
function isBlockedEdge(edgeId) {
    let edge = edges.get(edgeId);
    if (edge && edge.blocked) {
        let from = nodes.get(edge.from)
        let to = nodes.get(edge.to)
        let result = (edge.blocked && !isBlocked(from) && !isBlocked(to))
        return result
    }
    return false
}

function updateBlockedNodes() {
    var ids = nodes.getIds();
    for (var i = 0; i < ids.length; i++) {
        var node = nodes.get(ids[i]);
        if ("ips" in node) {
            for (var j = 0; j < node.ips.length; j++) {
                if (contains(blockList, node.ips[j])) {
                    if  (!node.is_blocked) {
                        node.is_blocked = true;
                        nodes.update(node);
                    }
                } else {
                    if  (node.is_blocked) {
                        node.is_blocked = false;
                        nodes.update(node);
                    }
                }
            }
        }
    }
}

function updateAllowedNodes() {
    var ids = nodes.getIds();
    for (var i = 0; i < ids.length; i++) {
        var node = nodes.get(ids[i]);
        if ("ips" in node) {
            for (var j = 0; j < node.ips.length; j++) {
                if (contains(allowedList, node.ips[j])) {
                    if  (!node.is_excepted) {
                        node.is_excepted = true;
                        nodes.update(node);
                    }
                } else {
                    if  (node.is_excepted) {
                        node.is_excepted = false;
                        nodes.update(node);
                    }
                }
            }
        }
    }
}

function cleanNetwork() {
    var now = Math.floor(Date.now() / 1000);
    var delete_before = now - 600;
    var unhighlight_before = now - 30;

    var ids = nodes.getIds();
    for (var i = 0; i < ids.length; i++) {
        var node = nodes.get(ids[i]);
        if (node.lastseen < delete_before) {
            deleteNode(node, true);
        } else if (node.lastseen < unhighlight_before) {
            if (node["color"]["background"] == colour_recent) {
                node["color"]["background"] = colour_dst;
                nodes.update(node);
            }
        }


    }
}


/* 
 On close of NodeInfo window
 */
function nodeInfoClosed(event, ui) {
    selectedNodeId = 0;
    $("#nodeinfo-peakdet").hide(); // Hide just to be sure.
    if (graph_peak_packets != null){
        graph_peak_packets.destroy();
        graph_peak_packets = null;
    }
    if (graph_peak_bytes != null){
        graph_peak_bytes.destroy();
        graph_peak_bytes = null;
    }
}

/*
 * On close of FlowInfo window
 */
function flowInfoClosed(event, ui) {
    selectedEdgeId = 0;
}

/* 
 * Request peak information for a particular nodeId.
 * Uses MQTT to query any running peak detection.
 */
function getPeakInformation() {
    if (selectedNodeId > 0) {
        sendCommand("get_peak_info", selectedNodeId);
    }
}

/* Handle peak information.
 * Only when information window for a local node is open.
 * If closed, stop streaming of information
 */
function handlePeakInformation(result) {
    var container = document.getElementById('nodeinfo-peakdetvis');
    $("#nodeinfo-peakdet").show()
    /*
     * If there is no graph yet, make one.
     * Otherwise, change only the inner dataset.
     */
    var options = {
        start: "2019-01-01 00:00",
        end: "2019-01-01 00:59",
        graphHeight: '100px',
        showMajorLabels: false,
        showMinorLabels: false,
        zoomable: false,
        moveable: false,
        legend: false,
        dataAxis: {
            visible: false,
            left: { range: { min: 0 }}
        },
        drawPoints: { enabled: false }
    };

    if (graph_peak_bytes == null) {
        // Remove loading text
        $("#nodeinfo-peakdetvis").html("");

        graph_peak_bytes = new vis.Graph2d(container, new vis.DataSet(), options);
        var groups = new vis.DataSet();
        groups.add({
            id: 0,
            content: "Bytes",
            style:   "stroke: blue"
        });
        groups.add({
            id: 1,
            style:   "stroke: red",
            options: {
                excludeFromLegend: "true"
            }
        });
        graph_peak_bytes.setGroups(groups);
    }

    if (graph_peak_packets == null) {
        graph_peak_packets = new vis.Graph2d(container, new vis.DataSet(), options);
        var groups = new vis.DataSet();
        groups.add({
            id: 0,
            content: "Packets",
            style:   "stroke: black"
        });
        groups.add({
            id: 1,
            style:   "stroke: red",
            options: {
                excludeFromLegend: true
            }
        });
        graph_peak_packets.setGroups(groups);
    }
    
    // Clear dataset, re-add changed values.
    graph_peak_bytes.itemsData.clear();
    graph_peak_packets.itemsData.clear();

    Object.keys(result["items"]).forEach(function (element){
        var elemi = parseInt(element);
        if (elemi < -59 || elemi > 0) {
            // We accept only -59 <= x <= 0
            return;
        }
        var m = 59 + elemi; // Compute: -1 goes to 58 seconds, Basically inverse all values.
        graph_peak_bytes.itemsData.add({
            x: "2019-01-01 00:" + String(m).padStart(2,'0'),
            y: result["items"][element]["bytes"],
            group: 0
        });

        graph_peak_packets.itemsData.add({
            x: "2019-01-01 00:" + String(m).padStart(2,'0'),
            y: result["items"][element]["packets"],
            group: 0
        });
    });
    graph_peak_bytes.redraw();

    // Now add limits
    if (result["maxbytes"] > 0) {
        graph_peak_bytes.itemsData.add({
            x: "2019-01-01 00:00",
            y: result["maxbytes"],
            group: 1
        });
        graph_peak_bytes.itemsData.add({
            x: "2019-01-01 00:59",
            y: result["maxbytes"],
            group: 1
        });
    }

    if (result["maxpackets"] > 0) {
        graph_peak_packets.itemsData.add({
            x: "2019-01-01 00:00",
            y: result["maxpackets"],
            group: 1
        });
        graph_peak_packets.itemsData.add({
            x: "2019-01-01 00:59",
            y: result["maxpackets"],
            group: 1
        });
    }
    
    if (result["maxbytes"] == 0 || result["maxpackets"] == 0) {
        $("#nodeinfo-notenoughdata:hidden").show();
    } else {
        $("#nodeinfo-notenoughdata:visible").hide();
    }
    
    if (!result["enforcing"]) {
        $("#nodeinfo-training:hidden").show();
    } else {
        $("#nodeinfo-training:visible").hide();
    }
}
