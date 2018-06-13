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
var selectedNodeId;
// list of filters
var filterList = [];
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
function sendAddFilterCommand(nodeId) {
    var node = nodes.get(nodeId);
    if ("ips" in node) {
        for (var i = 0; i < node.ips.length; i++) {
            filterList.push(node.ips[i]);
        }
    }

    sendCommand("add_filter", nodeId); // talk to websocket
    deleteNodeAndConnectedNodes(node);
}

function updateFilterList() {
    $("#filter-list").empty();
    for (var i = 0; i < filterList.length; i++) {
        var li = $("<li class='ui-widget-content'></li> ").text(filterList[i]);
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
        }
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
                    sendAddFilterCommand(selectedNodeId);
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
            sendCommand("add_name", argument); // talk to Websocket

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
                    $(".ui-dialog-buttonpane button:contains('Remove Filters')").button("enable");
                } else {
                    $(".ui-dialog-buttonpane button:contains('Remove Filters')").button("disable");
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
                "Remove Filters": function() {
                    $("#filter-list .ui-selected", this).each(function() {
                        // The inner text contains the name of the filter
                        //alertWithObject("[XX] selected:", this);
                        var address;
                        if (this.innerText) {
                            sendCommand("remove_filter", this.innerText);
                        } else if (this.innerHTML) {
                            sendCommand("remove_filter", this.innerHTML);
                        }
                        $(".ui-dialog-buttonpane button:contains('Remove Filters')").button("disable");
                    });
                },
                Reset: function() {
                    sendCommand("reset_filters", "");
                },
                Close: function() {
                    dialog.dialog("close");
                }
            },
            close: function() {}
        });

        $(".ui-dialog-buttonpane button:contains('Remove Filters')").button("disable");
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
                        //alertWithObject("[XX] selected:", this);
                        var address;
                        if (this.innerText) {
                            sendCommand("remove_block_ip", this.innerText);
                        } else if (this.innerHTML) {
                            sendCommand("remove_block_ip", this.innerHTML);
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
                        //alertWithObject("[XX] selected:", this);
                        var address;
                        if (this.innerText) {
                            sendCommand("remove_allow_ip", this.innerText);
                        } else if (this.innerHTML) {
                            sendCommand("remove_allow_ip", this.innerHTML);
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
            sendCommand("remove_block_node", selectedNodeId);
            node.is_blocked = false;
        } else {
            sendCommand("add_block_node", selectedNodeId);
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
            sendCommand("remove_allow_node", selectedNodeId);
            node.is_excepted = false;
        } else {
            sendCommand("add_allow_node", selectedNodeId);
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
        // Much TODO here; port, etc. currently spin_webui.lua must
        // be started and lua-minittp installed
        var url = window.location.protocol + "//" + window.location.hostname +
        "/spin_api/tcpdump?device="+name;
        var w = window.open(url, name, "width=400,  height=300, scrollbars=yes");
    });

    showGraph(traffic_dataset);
    showNetwork();
    initTrafficDataView();

    // clean up every X seconds
    setInterval(cleanNetwork, 5000);
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
        }
    };
    network = new vis.Network(container, data, options);
    network.on("selectNode", nodeSelected);
    network.on("dragStart", nodeSelected);
    network.on("zoom", enableZoomLock);
}

function updateNodeInfo(nodeId) {
    var node = nodes.get(nodeId);
    writeToScreen("trafficcount", "Connections seen: " + node.count);
    writeToScreen("trafficsize", "Traffic size: " + node.size);
    writeToScreen("ipaddress", "");
    writeToScreen("lastseen", "Last seen: " + node.lastseen + " (" + new Date(node.lastseen * 1000) + ")");
    // TODO: mark that this is hw not ip

    return node;
}

function nodeSelected(event) {
    var nodeId = event.nodes[0];
    if (typeof(nodeId) == 'number' && selectedNodeId != nodeId) {
        var node = updateNodeInfo(nodeId);
        selectedNodeId = nodeId;
        writeToScreen("nodeid", "Node: " + nodeId);
        //sendCommand("arp2ip", node.address); // talk to Websocket
        if (node.mac) {
            writeToScreen("mac", "HW Addr: " + node.mac);
        } else {
            writeToScreen("mac", "");
        }
        if (node.ips) {
            writeToScreen("ipaddress", "IP: " + node.ips.join());
        } else {
            writeToScreen("ipaddress", "IP: ");
        }
        if (node.domains) {
            writeToScreen("reversedns", "DNS: " + node.domains.join());
        } else {
            writeToScreen("reversedns", "DNS: ");
        }

        updateBlockedButton();
        updateAllowedButton();

        //sendCommand("ip2hostname", node.address);
        //writeToScreen("netowner", "Network owner: &lt;searching&gt;");
        //sendCommand("ip2netowner", node.address); // talk to Websocket
        $("#nodeinfo").dialog('option', 'title', node.label);
        $("#nodeinfo").dialog('open');

    }
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

function updateNode(node) {
    if (!node) { return; }
    var enode = nodes.get(node.id);
    if (!enode) { return; }

    var label = node.id;
    // should only change color if it was recent and is no longer so
    // but that is for a separate loop (unless it's internal device
    // and we just discovered that, in which case we set it to _src)
    //var colour = node.blocked ? colour_blocked : colour_recent;
    var ips = node.ips ? node.ips : [];
    var domains = node.domains ? node.domains : [];
    if (node.name) {
        label = node.name;
    } else if (node.mac) {
        label = node.mac;
    } else if (domains.length > 0) {
        label = node.domains[0];
    } else if (ips.length > 0) {
        label = node.ips[0];
    }

    if (node.mac) {
        node.color = colour_src;
    } else {
        node.color = colour_recent;
    }

    enode.label = label;
    enode.ips = ips;
    enode.domains = domains;

    nodes.update(enode);

    if (node.id == selectedNodeId) {
        updateNodeInfo(node.id);
    }
}

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
        } else if (dnsquery && !nodes.get(node.id)) {
            // Node does not exist, dnsquery
            colour = colour_dns;
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
    if (enode) {
        // update is
        enode.label = label;
        enode.ips = ips;
        enode.domains = domains;
        enode.color = { 'background': colour, 'border': border_colour };
        enode.blocked = blocked;
        enode.lastseen = timestamp;
        enode.is_blocked = node.is_blocked;
        enode.is_excepted = node.is_excepted;
        nodes.update(enode);
    } else {
        // it's new
        nodes.add({
            id: node.id,
            mac: node.mac,
            ips: node.ips ? node.ips : [],
            domains: node.domains ? node.domains : [],
            label: label,
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
                min: 1,
                label: {
                    enabled: true
                }
            }
        });
    }
}

// Used in AddFlow()
function addEdge(from, to, colour) {
    var existing = edges.get({
        filter: function(item) {
            return (item.from == from.id && item.to == to.id);
        }
    });
    if (existing.length == 0) {
        edges.add({
            id: curEdgeId,
            from: from.id,
            to: to.id,
            color: {color: colour}
        });
        curEdgeId += 1;
    } else if (existing[0].color.color != colour) {
        // If color changed, update it!
        existing[0].color = {color: colour};
        edges.update(existing[0]);
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
function addFlow(timestamp, from, to, count, size) {
    // there may be some residual additions from a recently added
    // filter, so ignore those

    // TODO ignore for now, data structure of from and to changed
    for (var i = 0; i < to.ips.length; i++) {
      var ip = to.ips[i];
      if (contains(filterList, ip)) {
        return;
      }
    }
    for (var i = 0; i < from.ips.length; i++) {
      var ip = from.ips[i];
      if (contains(filterList, ip)) {
        return;
      }
    }
    addNode(timestamp, from, false, count, size, "to " + to, "source");
    addNode(timestamp, to, true, count, size, "from " + from, "traffic");
    addEdge(from, to, colour_edge);
    if (!zoom_locked) {
        network.fit({
            duration: 0
        });
    }
}

function addBlocked(from, to) {
    if (contains(filterList, from) || contains(filterList, to)) {
        return;
    }

    // from["lastseen"] is the existing lastseen value, so we update
    // the from node with the lastseen value of packet too
    var timestamp = to["lastseen"];
    addNode(timestamp, from, false, 1, 1, "to " + to, "source");
    addNode(timestamp, to, false, 1, 1, "from " + from, "blocked");
    addEdge(from, to, colour_blocked);
    if (!zoom_locked) {
        network.fit({
            duration: 0
        });
    }
}

function addDNSQuery(from, dns) {
    if (contains(filterList, from) || contains(filterList, dns)) {
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

function updateBlockedNodes() {
    var ids = nodes.getIds();
    for (var i = 0; i < ids.length; i++) {
        var node = nodes.get(ids[i]);
        if ("ips" in node) {
            for (var j = 0; j < node.ips.length; j++) {
                //filterList.push(node.ips[j]);
                if (contains(blockList, node.ips[j])) {
                    //alert("[XX] node " + i + " with ips: " + node.ips + "in block list!");
                    if  (!node.is_blocked) {
                        node.is_blocked = true;
                        nodes.update(node);
                    }
                } else {
                    //alert("[XX] node " + i + " with ips: " + node.ips + "not in block list");
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
                //filterList.push(node.ips[j]);
                if (contains(allowedList, node.ips[j])) {
                    //alert("[XX] node " + i + " with ips: " + node.ips + "in allowed list!");
                    if  (!node.is_excepted) {
                        node.is_excepted = true;
                        nodes.update(node);
                    }
                } else {
                    //alert("[XX] node " + i + " with ips: " + node.ips + "not in allowed list");
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
