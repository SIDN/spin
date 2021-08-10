/* MQTT component of the Network Management Center (NMC)
 * Takes care of connection to MQTT broker
 */

package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"sync"

	"github.com/eclipse/paho.mqtt.golang"
)

type SPINnode struct {
	Id        int      `json:"id,omitempty"`
	Name      string   `json:"name,omitempty"`
	Mac       string   `json:"mac,omitempty"`
	Lastseen  int      `json:"lastseen,omitempty"`
	Ips       []string `json:"ips,omitempty"`
	Domains   []string `json:"domains,omitempty"`
	IsBlocked string   `json:"is_blocked,omitempty"`
}

type SPINflow struct {
	From      SPINnode `json:"from,omitempty"`
	To        SPINnode `json:"to,omitempty"`
	From_port int      `json:"from_port,omitempty"`
	To_port   int      `json:"to_port,omitempty"`
	Protocol  int      `json:"protocol,omitempty"`
	Size      int      `json:"size,omitempty"`
	Count     int      `json:"count,omitempty"`
}

type SPINresult struct {
	Flows       []SPINflow `json:"flows,omitempty"` // in case of flow data
	Timestamp   int        `json:"timestamp,omitempty"`
	Total_size  int        `json:"total_size,omitempty"`
	Total_count int        `json:"total_count,omitempty"`
	From        SPINnode   `json:"from,omitempty,omitempty"`        // in case of dnsquery
	Queriednode SPINnode   `json:"queriednode,omitempty,omitempty"` // in case of dnsquery
	Query       string     `json:"query,omitempty"`                 // in case of dnsquery
}

type SPINdata struct {
	Command  string     `json:"command"`
	Argument string     `json:"argument"`
	Result   SPINresult `json:"result"`
}

type SPINfilter struct { // Typically used for blocks, filter lists etc.
	Command string   `json:"command"`
	Result  []string `json:"result"`
}

// Used to send commands to the server
type SPINcommand struct {
	Command  string `json:"command"`
	Argument int    `json:"argument"`
}

var client mqtt.Client

var mqttsubscribers = struct {
	sync.RWMutex
	Data   []chan SPINdata
	Filter []chan SPINfilter
}{Data: []chan SPINdata{},
	Filter: []chan SPINfilter{}}

const SPIN_CMD_ADD_BLOCK = "add_block_node"
const SPIN_CMD_REMOVE_BLOCK = "remove_block_node"
const TOPIC_TRAFFIC = "SPIN/traffic"
const TOPIC_COMMANDS = "SPIN/commands"

func ConnectToBroker(ip string, port string) mqtt.Client {
	// Connect to message broker, returns new Client.
	opts := mqtt.NewClientOptions().AddBroker("tcp://" + ip + ":" + port)
	opts.SetClientID("spin-nms")                       // our identifier
	opts.SetAutoReconnect(true)                        // once connected, always reconnect
	opts.SetOnConnectHandler(onConnectHandler)         // when (re)connected
	opts.SetConnectionLostHandler(onDisconnectHandler) // when connection is lost
	client = mqtt.NewClient(opts)

	fmt.Println("Connecting...")
	if token := client.Connect(); token.Wait() && token.Error() != nil {
		fmt.Println("Error: ", token.Error())
	}

	return client
}

func KillBroker() {
	mqttsubscribers.RLock()
	for _, s := range mqttsubscribers.Data {
		close(s)
	}
	for _, s := range mqttsubscribers.Filter {
		close(s)
	}
	defer mqttsubscribers.RUnlock()
	client.Disconnect(250) // disconnect and wait 250ms for it to finish
}

func onConnectHandler(client mqtt.Client) {
	// fired when a connection has been established. Either the initial, or a reconnection
	fmt.Printf("Connected to server.\n")
	if token := client.Subscribe(TOPIC_TRAFFIC, 0, messageHandler); token.Wait() && token.Error() != nil {
		fmt.Println("Unable to subscribe", token.Error())
		os.Exit(1)
	}
}

func onDisconnectHandler(client mqtt.Client, err error) {
	// fired when the connection was lost unexpectedly.
	// not fired on intented disconnect
	fmt.Println("Disconnected: ", err)
}

func messageHandler(client mqtt.Client, msg mqtt.Message) {
	//fmt.Printf("TOPIC: %s\n", msg.Topic())
	//fmt.Printf("MSG: %s\n", msg.Payload())

	var parsed SPINdata
	var parsedf SPINfilter
	err := json.Unmarshal(msg.Payload(), &parsed)
	if err != nil {
		// Try parsing as filter!
		err := json.Unmarshal(msg.Payload(), &parsedf)
		if err != nil {
			fmt.Println("Error while parsing", err)
			fmt.Println("JSON: ", string(msg.Payload()))
			return
		}
		go notifyFilter(parsedf)
	} else {
		go notifyData(parsed)
	}
}

func BrokerSubscribeData() chan SPINdata {
	mqttsubscribers.Lock() // obtain a write-lock
	defer mqttsubscribers.Unlock()
	ch := make(chan SPINdata, CHANNEL_BUFFER)
	mqttsubscribers.Data = append(mqttsubscribers.Data, ch)
	return ch
}

func notifyData(data SPINdata) {
	subscribers.RLock()
	defer subscribers.RUnlock()

	for _, ch := range mqttsubscribers.Data {
		ch <- data
	}
}

func BrokerSubscribeFilter() chan SPINfilter {
	mqttsubscribers.Lock() // obtain a write-lock
	defer mqttsubscribers.Unlock()
	ch := make(chan SPINfilter, CHANNEL_BUFFER)
	mqttsubscribers.Filter = append(mqttsubscribers.Filter, ch)
	return ch
}

func notifyFilter(data SPINfilter) {
	subscribers.RLock()
	defer subscribers.RUnlock()

	for _, ch := range mqttsubscribers.Filter {
		ch <- data
	}
}

func BrokerSendCommand(command SPINcommand) {
	// Sends command back to the broker
	// Publish(topic string, qos byte, retained bool, payload interface{}) Token
	bcmd, err := json.Marshal(command)
	if err != nil {
		fmt.Println("Error while making JSON of command", command.Command, command.Argument)
		return
	}
	err = BrokerSend(bcmd, TOPIC_COMMANDS)
	if err != nil {
		fmt.Println(err)
	}
}

func BrokerSend(message []byte, topic string) error {
	if token := client.Publish(topic, 0, false, message); token.Wait() && token.Error() != nil {
		errors.New(fmt.Sprintf("MQTT: Error sending message: %v", token.Error()))
	}
	return nil
}

func BrokerSubscribe(topic string) (chan []byte, error) {
	// Generic handler for MQTT subscriptions
	// returns channel to listen to for events
	ch := make(chan []byte, CHANNEL_BUFFER)
	if client == nil {
		return nil, errors.New("No client available")
	}
	if token := client.Subscribe(topic, 0, func(client mqtt.Client, msg mqtt.Message) {
		ch <- msg.Payload()
	}); token.Wait() && token.Error() != nil {
		return nil, errors.New(fmt.Sprintf("Unable to subscribe: %v", token.Error()))
	}
	return ch, nil
}
