/*
 * SPIN Network Management Center (NMC)
 * Made by SIDN Labs (sidnlabs@sidn.nl)
 */
package main

import (
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"
)

const CHANNEL_BUFFER = 100

func main() {
	// Parse commandline arguments
	freshPtr := flag.Bool("fresh", false, "start fresh, i.e. do not restore previous state")
	restoreFilePtr := flag.String("db", ".spin-nmc-history.db", "restore database file")
	mqttHostPtr := flag.String("mqtthost", "valibox.", "Host of mqtt server")
	mqttPortPtr := flag.String("mqttport", "1883", "Port of mqtt server")
	flag.Parse()

	var hs *HistoryDB = nil
	var as *map[int]*FlowSummary = nil
	if !*freshPtr {
		/* Continue from old state, if present */
		persist, err := load(*restoreFilePtr)
		if err != nil {
			if !os.IsNotExist(err) {
				fmt.Println("Error on loading old state:", err)
			}
			/* Otherwise, file does not exist, not an error, just nothing to load from */
		} else {
			hs = &persist.HistoryState
			as = &persist.TrafficHistoryState
		}
	}
	InitHistory(hs) // initialize history service
	InitAnomaly(as) // Anomaly detection

	// Connect to MQTT Broker of valibox
	ConnectToBroker(*mqttHostPtr, *mqttPortPtr)
	HandleKillSignal()

	for {
		time.Sleep(5 * time.Minute)
		if save(*restoreFilePtr) {
			fmt.Println("Saved state to disk")
		} else {
			fmt.Println("Error, unable to save state to disk")
		}
		// History.RLock()
		// fmt.Printf("History: %+v\n", History)
		// History.RUnlock()
	}
}

// Handle kill signals for all modules
func HandleKillSignal() {
	// Set a signal handler
	csig := make(chan os.Signal, 2)
	signal.Notify(csig, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-csig
		fmt.Println("\nShutting down...")
		KillBroker()
		KillHistory()
		os.Exit(1)
	}()
}
