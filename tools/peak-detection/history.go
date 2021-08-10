/*
 * History service for SPIN-NMC
 * Made by SIDN Labs (sidnlabs@sidn.nl)
 */

/*
 * The SPIN device (agent) may restart and use different SPIN identifiers for the same device.
 * We have to account for that.
 */

package main

import (
	"fmt"
	"net"
	"sync"
	"time"
)

var History = struct {
	sync.RWMutex
	m           HistoryDB
	initialised bool
}{m: HistoryDB{}, initialised: false}

type HistoryDB struct {
	Devices map[int]Device `json:"devices"` // map that lists all devices
}

// Flow represents an aggregated type of flow for a single device.
// This means multiple flows to the same ip/port combination are considered one.
type Flow struct {
	RemoteIps       []net.IP  `json:"remoteips"`       // ip addresses
	NodeId          int       `json:"nodeid"`          // SPIN Identifier for this node
	BytesReceived   int       `json:"bytesreceived"`   // Number of bytes received by the local device
	BytesSent       int       `json:"bytessent"`       // Number of bytes sent by the local device to the remote one
	PacketsReceived int       `json:"packetsreceived"` // Number of packets received
	PacketsSent     int       `json:"packetssent"`     // Number of packets sent
	RemotePort      int       `json:"remoteport"`      // Port of remote server
	FirstActivity   time.Time `json:"firstactivity"`   // First time that activity was logged
	LastActivity    time.Time `json:"lastactivity"`    // Last activity of this flow
}

type Device struct {
	Mac       net.HardwareAddr    `json:"mac"`       // Mac address of this device.
	SpinId    int                 `json:"spinid"`    // SPIN node identifier, used for quick verification of MAC. And internal references.
	Lastseen  time.Time           `json:"lastseen"`  // Timestamp of last moment the device sent or received traffic
	Flows     []Flow              `json:"flows"`     // An array of flows for this device
	Resolved  map[string][]net.IP `json:"resolved"`  // Resolved domains for this device. The key is the DNS request (domain).
	Addresses []net.IP            `json:"addresses"` // local addresses at which this device is known
}

var subscribers = struct {
	sync.RWMutex
	ExtraTraffic []chan SubFlow // subscribers that want repeat traffic. Provides a Flow.
	Resolve      []chan SubDNS  // subscribers that want all DNS resolver data. Provides SubFlow struct
	NewDevice    []chan int     // subscribers that want to get informed about new devices. Providers int
	NewTraffic   []chan SubFlow // subscribers that want to get only new flows
}{ExtraTraffic: []chan SubFlow{},
	Resolve:    []chan SubDNS{},
	NewDevice:  []chan int{},
	NewTraffic: []chan SubFlow{}}

type SubDNS struct {
	Deviceid int      // SPIN id for the device
	Request  string   // DNS request, e.g.: example.nl
	Reply    []net.IP // DNS reply, e.g.: 127.0.0.1
}

type SubFlow struct {
	Deviceid        int // SPIN id for the device
	Flowid          int // The Flow id with changes
	BytesReceived   int // Number of bytes received by the local device
	BytesSent       int // Number of bytes sent by the local device to the remote one
	PacketsReceived int // Number of packets received
	PacketsSent     int // Number of packets sent
}

// channel to which we listen for messages
var brokerchan chan SPINdata

// Initialisation
func InitHistory(stored *HistoryDB) {
	// Initialize history service
	// if load: tries to reload from disk

	History.Lock() // obtain write-lock
	defer History.Unlock()

	if stored != nil {
		fmt.Println("InitHistory(): loading from disk")
		History.m = *stored
	}

	if History.m.Devices == nil {
		History.m.Devices = make(map[int]Device)
	}
	brokerchan = BrokerSubscribeData()
	go func() {
		for {
			data, ok := <-brokerchan
			if !ok {
				break
			}
			HistoryAdd(data)
		}
	}()
	History.initialised = true
}

// Adds a flow or dnsquery to the history file
func HistoryAdd(msg SPINdata) bool {
	History.RLock()
	initialised := History.initialised
	History.RUnlock()

	if !initialised {
		// If the History file was not initialised yet, do so now
		InitHistory(nil)
	}

	switch msg.Command {
	case "traffic":
		// do this
		//fmt.Println("Flow found", msg.Result.Flows[0].From.Id, "to", msg.Result.Flows[0].To.Id)

		History.Lock() // obtain write-lock
		defer History.Unlock()

		// Start parsing all flows
		for _, flow := range msg.Result.Flows {
			// flow
			var local, remote SPINnode
			var remoteport int

			if len(flow.From.Mac) > 0 {
				local, remote, remoteport = flow.From, flow.To, flow.To_port
			} else if len(flow.To.Mac) > 0 {
				local, remote, remoteport = flow.To, flow.From, flow.From_port
			} else {
				fmt.Println("HistoryAdd(): Unable to process flow, cannot find local device.")
				break
			}

			// Now, process flow
			deviceid := local.Id
			dev := getDevice(deviceid)

			// Compute relevant variables from flow
			ips := []net.IP{}
			for _, v := range remote.Ips {
				ips = append(ips, net.ParseIP(v))
			}

			byReceived, bySent, packReceived, packSent := 0, 0, 0, 0
			if local.Id == flow.From.Id {
				byReceived, bySent, packReceived, packSent = 0, flow.Size, 0, flow.Count
			} else {
				byReceived, bySent, packReceived, packSent = flow.Size, 0, flow.Count, 0
			}

			idx, histflow := findFlow(dev.Flows, remote.Id, remoteport)
			if idx < 0 {
				// create new
				histflow = Flow{RemoteIps: ips, NodeId: remote.Id, RemotePort: remoteport, BytesReceived: byReceived,
					BytesSent: bySent, PacketsReceived: packReceived, PacketsSent: packSent,
					FirstActivity: time.Unix(int64(msg.Result.Timestamp), 0),
					LastActivity:  time.Unix(int64(msg.Result.Timestamp), 0)}
				dev.Flows = append(dev.Flows, histflow)
				idx, _ := findFlow(dev.Flows, remote.Id, remoteport) // Obtain index of newly added flow
				go notifyNewTraffic(deviceid, idx, byReceived, bySent, packReceived, packSent)
			} else {
				// update
				histflow.RemoteIps = mergeIP(histflow.RemoteIps, ips)
				histflow.BytesReceived += byReceived
				histflow.BytesSent += bySent
				histflow.PacketsReceived += packReceived
				histflow.PacketsSent += packSent
				histflow.LastActivity = time.Unix(int64(msg.Result.Timestamp), 0)
				dev.Flows[idx] = histflow
				go notifyExtraTraffic(deviceid, idx, byReceived, bySent, packReceived, packSent)
			}

			// Store results
			History.m.Devices[deviceid] = dev
		}

		// first, we need to determine which of the nodes is the local device

		// Probably, it had DNS traffic before, so if no MAC address is set, do so now
		// But, if a MAC is set, MAC takes priority over spin identifier

		return true
	case "dnsquery":
		// do that
		deviceid := msg.Result.From.Id

		History.Lock() // obtain write-lock
		defer History.Unlock()

		dev := getDevice(deviceid)

		dnsq, exists := dev.Resolved[msg.Result.Query]
		// check if dns query exists, if not: make new one
		if !exists {
			dnsq = []net.IP{}
		}

		// merge sets of resolved IPs
		rip := []net.IP{}
		for _, i := range msg.Result.Queriednode.Ips {
			rip = append(rip, net.ParseIP(i))
		}
		dnsq = mergeIP(dnsq, rip)

		// and merge dnsq back to dns
		dev.Resolved[msg.Result.Query] = dnsq

		// merge set of node ips
		rip = []net.IP{}
		for _, i := range msg.Result.From.Ips {
			rip = append(rip, net.ParseIP(i))
		}
		dev.Addresses = mergeIP(dev.Addresses, rip)

		// and update the lastseen field
		dev.Lastseen = time.Unix(int64(msg.Result.From.Lastseen), 0)

		// dev now contains all updates
		// put results back to History
		History.m.Devices[deviceid] = dev

		go notifyResolve(deviceid, msg.Result.Query, rip)

		return true
	}
	return false
}

// Requires Read lock on the History
// Returns device information, or returns new one
func getDevice(deviceid int) Device {
	dev, exists := History.m.Devices[deviceid]
	// If not yet there, make an empty one
	if !exists {
		dev = Device{Mac: nil, SpinId: deviceid, Lastseen: time.Now(),
			Flows: []Flow{}, Resolved: make(map[string][]net.IP),
			Addresses: []net.IP{}}
		go notifyNewDevice(deviceid) // notify interested parties
	}
	return dev
}

// Merges two lists of ip addresses
func mergeIP(ip1 []net.IP, ip2 []net.IP) []net.IP {
	for _, ip := range ip2 {
		found := false
		for _, comp := range ip1 {
			if comp.Equal(ip) {
				found = true
				break
			}
		}
		if !found {
			ip1 = append(ip1, ip)
		}
	}
	return ip1
}

// Subscribe to DNS resolve results
func SubscribeResolve() chan SubDNS {
	subscribers.Lock()
	defer subscribers.Unlock()

	ch := make(chan SubDNS, CHANNEL_BUFFER)
	subscribers.Resolve = append(subscribers.Resolve, ch)
	return ch
}

func notifyResolve(deviceid int, request string, reply []net.IP) {
	// make new SubDNS
	msg := SubDNS{Deviceid: deviceid, Request: request, Reply: reply}

	subscribers.RLock()
	defer subscribers.RUnlock()
	for _, ch := range subscribers.Resolve {
		ch <- msg
	}
}

// New device
func SubscribeNewDevice() chan int {
	subscribers.Lock()
	defer subscribers.Unlock()

	ch := make(chan int, CHANNEL_BUFFER)
	subscribers.NewDevice = append(subscribers.NewDevice, ch)
	return ch
}

func notifyNewDevice(dev int) {
	subscribers.RLock()
	defer subscribers.RUnlock()
	for _, ch := range subscribers.NewDevice {
		ch <- dev
	}
}

// New Traffic
func SubscribeNewTraffic() chan SubFlow {
	subscribers.Lock()
	defer subscribers.Unlock()

	ch := make(chan SubFlow, CHANNEL_BUFFER)
	subscribers.NewTraffic = append(subscribers.NewTraffic, ch)
	return ch
}

func notifyNewTraffic(deviceid int, flowid int, byRecv int, bySent int, paRecv int, paSent int) {
	subscribers.RLock()
	defer subscribers.RUnlock()

	for _, ch := range subscribers.NewTraffic {
		// We make a new flow for every subscriber, so that they will not bother eachother
		msg := SubFlow{Deviceid: deviceid, Flowid: flowid, BytesReceived: byRecv, BytesSent: bySent,
			PacketsReceived: paRecv, PacketsSent: paSent}
		ch <- msg
	}
}

// Traffic to existing flow
func SubscribeExtraTraffic() chan SubFlow {
	subscribers.Lock()
	defer subscribers.Unlock()

	ch := make(chan SubFlow, CHANNEL_BUFFER)
	subscribers.ExtraTraffic = append(subscribers.ExtraTraffic, ch)
	return ch
}

func notifyExtraTraffic(deviceid int, flowid int, byRecv int, bySent int, paRecv int, paSent int) {
	subscribers.RLock()
	defer subscribers.RUnlock()

	for _, ch := range subscribers.ExtraTraffic {
		msg := SubFlow{Deviceid: deviceid, Flowid: flowid, BytesReceived: byRecv, BytesSent: bySent,
			PacketsReceived: paRecv, PacketsSent: paSent}
		ch <- msg
	}
}

// Makes sure to deep copy a flow.
// All normal variables are copied by value already.
// Slices need a copy()
func flowdup(flow Flow) Flow {
	tmp := flow // now tmp contains copy-by-ref slice
	// manually copy slice into tmp
	copy(tmp.RemoteIps, flow.RemoteIps)
	return tmp
}

// Requires at least a read lock on History
// Returns index and the corresponding Flow, or index = -1 if no flows were found
// The index is only valid until you release the read lock
func findFlow(flows []Flow, id int, port int) (int, Flow) {
	for idx, flow := range flows {
		if flow.NodeId == id && flow.RemotePort == port {
			return idx, flow
		}
	}
	return -1, Flow{}
}

// Returns a list of all devices
func HistoryListDevices() []int {
	History.RLock()
	defer History.RUnlock()
	return listDevices()
}

// Requires read lock
func listDevices() []int {
	keys := []int{}
	for i, _ := range History.m.Devices {
		keys = append(keys, i)
	}
	return keys
}

// Try to figure out which DNS lookups correspond with a list of IPs
// Tries first only the device itself
// If fail, tries to search lookups from other devices
// If that fails too, returns an empty list
func IPToName(deviceid int, ips []net.IP) []string {
	found := map[string]struct{}{}
	History.RLock()
	defer History.RUnlock()
	dev := getDevice(deviceid)
	if len(dev.Resolved) > 0 {
		for host, iplist := range dev.Resolved {
			for _, ip := range iplist {
				for _, test := range ips {
					if test.Equal(ip) {
						found[host] = struct{}{}
					}
				}
			}
		}
	} // else: new device, or nothing resolved yet
	out := []string{}
	for hostname := range found {
		out = append(out, hostname)
	}
	return out
}

func KillHistory() {
	// Immediate shutdown
	subscribers.RLock()
	defer subscribers.RUnlock()

	for _, ch := range subscribers.Resolve {
		close(ch)
	}
	time.Sleep(250 * time.Millisecond)
}
