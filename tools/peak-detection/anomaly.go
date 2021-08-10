/*
 * Anomaly detection for SPIN-NMC
 * Made by SIDN Labs (sidnlabs@sidn.nl)
 */

/*
 * TODO:
 * - store state for later re-use?
 * - Currently only does peak detection, expand?
 */

/*
 * This module implements a simple peak-based anomaly detection.
 * We store all previous flow-information per time period.
 * The first x hours, we only monitor, after that, we will start enforcing
 */

package main

import (
	"encoding/json"
	"fmt"
	"sync"
	"time"
)

const RECENT_TRAFFIC = 5               // Last 5 minutes is recent traffic
const PEAK_MAX_INCREASE = 1.2          // Alert if new peak is 20% higher than old one.
const PEAK_THRESHOLD = 100 * 1024 * 60 // 100 Kbit/sec (over a minute interval) is always allowed
const PENALTY_THRESHOLD = 3            // If 3 out of 5 minutes the peak is too high, block!
const TIME_MEASURING = 10              // In minutes, time to measure (10 means monitor for 0-10 minutes)
const TIME_REPORTING = 60              // Time to report only, no blocking, in minutes. Longer than this will be blocked.

type Datapoint struct {
	BytesReceived   int // Number of bytes received by the local device
	BytesSent       int // Number of bytes sent by the local device to the remote one
	PacketsReceived int // Number of packets received
	PacketsSent     int // Number of packets sent
}

type FlowSummary struct {
	NodeId     int
	Datapoints map[time.Time]*Datapoint // List of all datapoints within this flow. Per-minute interval.
}

// var trafficHistory []FlowSummary // Our own database with all historic flows
var TrafficHistory = struct {
	sync.RWMutex
	h           map[int]*FlowSummary // index is SPIN Identifier for this node. Same as in History database.
	initialised bool
}{h: map[int]*FlowSummary{}}

// Initialise anomaly detection
// if load, reload state from disk (filepath in attach)
func InitAnomaly(oldstate *map[int]*FlowSummary) {
	if oldstate != nil {
		TrafficHistory.Lock()
		defer TrafficHistory.Unlock()
		TrafficHistory.h = *oldstate
	}
	// go printResolved(SubscribeResolve())
	// go printNewTraffic(SubscribeNewTraffic())
	go processTraffic(SubscribeNewTraffic())
	go processTraffic(SubscribeExtraTraffic())
	go listenWebInfo()
}

// Process new datapoint to existing flow, or new flow.
func processTraffic(ch chan SubFlow) {
	for {
		flowinfo, cont := <-ch
		if !cont { // channel is closed
			break
		}
		deviceid := flowinfo.Deviceid
		// fmt.Printf("AD: Device %v adding flowdata to flow %v with recv:%v/%v sent:%v/%v bytes/packets\n", deviceid, flowinfo.Flowid,
		//   flowinfo.BytesReceived, flowinfo.PacketsReceived, flowinfo.BytesSent, flowinfo.PacketsSent)

		TrafficHistory.Lock()
		flow, exists := TrafficHistory.h[deviceid]
		t := getRoundedMinute(time.Now())

		if !exists {
			// No FlowSummary for this device
			flow = &FlowSummary{NodeId: deviceid, Datapoints: make(map[time.Time]*Datapoint)}
			flow.Datapoints[t] = &Datapoint{BytesReceived: 0,
				BytesSent: 0, PacketsReceived: 0, PacketsSent: 0}
		}
		dp := flow.Datapoints[t]
		if dp == nil {
			/* Start a new minute */
			dp = &Datapoint{0, 0, 0, 0}
			flow.Datapoints[t] = dp
			go analyseTraffic(deviceid) // Call peak detection
		}
		dp.BytesReceived += flowinfo.BytesReceived
		dp.BytesSent += flowinfo.BytesSent
		dp.PacketsReceived += flowinfo.PacketsReceived
		dp.PacketsSent += flowinfo.PacketsSent
		TrafficHistory.h[deviceid] = flow
		// fmt.Printf("History: %+v\n", TrafficHistory)
		// fmt.Printf("FlowSummary: %v\n", TrafficHistory.h)
		// for _, m := range TrafficHistory.h {
		//   fmt.Printf("\t%v\n", m)
		//   for _, n := range m.Datapoints {
		//     fmt.Printf("\t\t%v\n", n)
		//   }
		// }
		TrafficHistory.Unlock()
	}
}

// Debug print functions

// func printResolved(ch chan SubDNS) {
//   for {
//     req, cont := <-ch
//     if !cont { // channel is closed
//       break
//     }
//     fmt.Println("AD: device ", req.Deviceid, " DNS request = ", req.Request)
//   }
// }
//
// func printNewTraffic(ch chan SubFlow) {
//   for {
//     flowinfo, cont := <-ch
//     if !cont { // channel is closed
//       break
//     }
//     hostnames := IPToName(flowinfo.Deviceid, flowinfo.NewFlow.RemoteIps)
//     if len(hostnames) == 0 {
//       for _, ip := range flowinfo.NewFlow.RemoteIps {
//         hostnames = append(hostnames, ip.String())
//       }
//     }
//     for _, host := range hostnames {
//       fmt.Print(" ", host)
//     }
//     fmt.Println(" on port", flowinfo.NewFlow.RemotePort)
//   }
// }

// Gets time and returns rounded (to the minute) version
func getRoundedMinute(t time.Time) time.Time {
	return time.Date(t.Year(), t.Month(), t.Day(), t.Hour(), t.Minute(), 0, 0, time.Local)
}

/* Check whether there is a recent peak in outgoing traffic.
   Called every minute.
   Only outgoing traffic, to prevent DDoS attacks.
   Needs at least 10 minutes of data, but blocks only after 1 hour.
   Perform checking for the last 5 minutes.
   Calculates mean and std dev of device (alltime, and last day).
   Calculate absolute peak.

   Bug: 1 minute high spike, with no traffic afterwards, no analysis done.
      -> fix properly, this is just a POC.

   Returns: recent bytes, recent packets, maxbytes, maxpackets
*/
func getPeak(nodeid int) ([]int, []int, int, int, int, int) {
	TrafficHistory.RLock()
	defer TrafficHistory.RUnlock()
	_, exists := TrafficHistory.h[nodeid]
	if !exists {
		return []int{}, []int{}, 0, 0, 0, 0
	}
	dp := TrafficHistory.h[nodeid].Datapoints

	// At this moment: tmax is the lastest recorded time
	// fmt.Printf("AD: Analysing outgoing traffic of %v for %v\n", nodeid, tmax.Sub(tmin))

	recentmaxbytes := 0
	recentmaxpackets := 0
	recentbytes := []int{}
	recentpackets := []int{}
	maxbytes := 0
	maxpackets := 0

	for k, v := range dp {
		if time.Now().Sub(k).Minutes() <= RECENT_TRAFFIC {
			// Recent traffic of last RECENT_TRAFFIC (default 5) minutes
			// Update counters
			recentbytes = append(recentbytes, v.BytesSent)
			recentpackets = append(recentpackets, v.PacketsSent)

			if recentmaxbytes < v.BytesSent {
				recentmaxbytes = v.BytesSent
			}
			if recentmaxpackets < v.PacketsSent {
				recentmaxpackets = v.PacketsSent
			}
		} else {
			// Update statistics
			if maxbytes < v.BytesSent {
				maxbytes = v.BytesSent
			}
			if maxpackets < v.PacketsSent {
				maxpackets = v.PacketsSent
			}
		}
		// fmt.Printf("AD: device %v traffic %v/%v bytes and %v/%v packets (in/out) %v\n", nodeid, v.BytesReceived,
		//   v.BytesSent, v.PacketsReceived, v.PacketsSent, k)
	}
	return recentbytes, recentpackets, recentmaxbytes, recentmaxpackets, maxbytes, maxpackets
}

func getTimeMinMax(dp map[time.Time]*Datapoint) (time.Time, time.Time) {
	// Store all defaults
	tmax := time.Now().AddDate(-10, 0, 0) // 10 years in the past
	tmin := time.Now().AddDate(10, 0, 0)  // 10 years in the future

	for k := range dp {
		if tmax.Before(k) {
			tmax = k
		}
		if tmin.After(k) {
			tmin = k
		}
	}
	return tmin, tmax
}

func analyseTraffic(nodeid int) {
	// fmt.Println("AD: device", nodeid, "model (b/p): ", maxbytes, "/", maxpackets)
	recentbytes, recentpackets, recentmaxbytes, recentmaxpackets,
		maxbytes, maxpackets := getPeak(nodeid)

	// Continue only when a peak was found
	peak := false
	penaltyb, penaltyp := 0, 0
	for _, v := range recentbytes {
		if v >= PEAK_THRESHOLD && float64(v) > float64(maxbytes)*PEAK_MAX_INCREASE {
			penaltyb += 1
		}
	}
	for _, v := range recentpackets {
		if v >= PEAK_THRESHOLD && float64(v) > float64(maxpackets)*PEAK_MAX_INCREASE {
			penaltyp += 1
		}
	}

	peak = penaltyp >= PENALTY_THRESHOLD || penaltyb >= PENALTY_THRESHOLD

	TrafficHistory.RLock()
	dp := TrafficHistory.h[nodeid].Datapoints
	tmin, tmax := getTimeMinMax(dp)
	dp = nil
	TrafficHistory.RUnlock()

	duration := tmax.Sub(tmin).Minutes()
	switch {
	case duration < TIME_MEASURING: // Only measuring
	case peak && duration < TIME_REPORTING: // Reporting, not blocking
		fmt.Println("AD: PEAK device", nodeid, "has peak, no action taken:", recentmaxbytes,
			"/", recentmaxpackets, "bytes/packets", duration)
	case peak: // Block bad traffic!
		BrokerSendCommand(SPINcommand{SPIN_CMD_ADD_BLOCK, nodeid})

		fmt.Println("AD: BLOCKED device", nodeid, "for peak: ", recentmaxbytes,
			"/", recentmaxpackets, "bytes/packets", duration)
		fmt.Println("AD: device", nodeid, "had limit of", float64(maxbytes)*PEAK_MAX_INCREASE,
			"/", float64(recentmaxpackets)*PEAK_MAX_INCREASE, " bytes/packets", duration)
	default:
		fmt.Println("AD: device", nodeid, "all okay", recentmaxbytes, "/", recentmaxpackets,
			"bytes/packets", "limit", maxbytes, "/", maxpackets)
	}
}

func listenWebInfo() {
	brokerchan, brokererr := BrokerSubscribe(TOPIC_COMMANDS)
	if brokererr != nil {
		fmt.Println("listenWebInfo: unable to subscribe to commands topic")
		time.Sleep(1 * time.Second)
		go listenWebInfo()
		return
	}
	go func() {
		for {
			data, ok := <-brokerchan
			if !ok {
				break
			}
			var parsed SPINcommand
			err := json.Unmarshal(data, &parsed)
			if err != nil {
				continue
			}

			switch parsed.Command {
			case "get_peak_info":
				// Return peak information for node in arguments
				nodeid := parsed.Argument
				if nodeid <= 0 {
					continue
				}
				// var nodeid int
				// var err error
				// if nodeid, err = strconv.Atoi(nodeidstr); err != nil {
				//     continue
				// }
				traffic := make(map[string]interface{})
				items := make(map[string]interface{})

				_, _, _, _, maxbytes, maxpackets := getPeak(nodeid)
				traffic["maxbytes"] = int(float64(maxbytes) * PEAK_MAX_INCREASE)
				traffic["maxpackets"] = int(float64(maxpackets) * PEAK_MAX_INCREASE)

				TrafficHistory.RLock()

				node, exists := TrafficHistory.h[nodeid]
				results := make(map[string]interface{})
				if exists {
					dp := node.Datapoints
					tmin, tmax := getTimeMinMax(dp)
					traffic["enforcing"] = tmax.Sub(tmin).Minutes() >= TIME_REPORTING

					for k, v := range dp {
						if time.Now().Sub(k).Minutes() <= 60 {
							// Send back last 60 minutes of traffic
							items[fmt.Sprintf("%0.f", 0-time.Now().Sub(k).Minutes())] = map[string]interface{}{
								"bytes":   v.BytesSent,
								"packets": v.PacketsSent}
						}
					}
					traffic["items"] = items

					results["command"] = "peakinfo"
					results["argument"] = fmt.Sprintf("%v", nodeid)
					results["result"] = traffic

				} else {
					// Return error, no information.
					results["command"] = "peakinfo"
					results["argument"] = fmt.Sprintf("%v", nodeid)
				}

				bresults, err := json.Marshal(results)
				if err != nil {
					fmt.Println("Error while making JSON of peak info", nodeid)
					continue
				}
				BrokerSend(bresults, TOPIC_TRAFFIC)

				TrafficHistory.RUnlock()

			}
		}
	}()
}
