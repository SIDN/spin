/*
 * All procedures for saving and loading state
 * Problems to deal with in persistence:
 * - how to deal with node identifiers that have changed after a reboot?
 * - where to store the databasefile on OpenWRT so that is persists a reboot?
 */

package main

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"os"
)

type StorageState struct {
	HistoryState        HistoryDB            `json:"history,omitempty"`
	TrafficHistoryState map[int]*FlowSummary `json:"traffichistory,omitempty"`
}

func save(fp string) bool {
	History.RLock()
	TrafficHistory.RLock()
	defer History.RUnlock()
	defer TrafficHistory.RUnlock()
	ss := StorageState{History.m, TrafficHistory.h}
	return saveToFile(ss, fp)
}

func load(fp string) (StorageState, error) {
	f, err := os.Open(fp)
	if err != nil {
		return StorageState{}, err
	}
	defer f.Close()

	bbuf, err := ioutil.ReadAll(f)
	if err != nil {
		return StorageState{}, errors.New("Error: cannot load from file, failed read")
	}

	ss := StorageState{}
	err = json.Unmarshal(bbuf, &ss)
	if err != nil {
		return StorageState{}, errors.New("Error on loading state from json")
	}
	fmt.Println("Loaded state from disk", fp)
	return ss, nil
}

func saveToFile(ss StorageState, fp string) bool {
	b, err := json.Marshal(ss)
	if err != nil {
		fmt.Println("Error on dumping state to json:", err)
		return false
	}

	// Now writing to file
	f, err := os.Create(fp)
	if err != nil {
		fmt.Println("Error on opening file", fp, ":", err)
		return false
	}
	defer f.Close()

	w := bufio.NewWriter(f)
	n, err := w.Write(b)
	if err != nil {
		fmt.Println("Error on writing file", fp, ":", err)
		return false
	}

	w.Flush()
	fmt.Println("Wrote history to file in", n, "bytes")
	return true
}
