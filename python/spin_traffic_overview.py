#!/usr/bin/env python3

import argparse
import datetime
import json
import time
import sys
import paho.mqtt.client as mqtt

# 'flat' representation of flow;
# timestamp, from_mac, from_ips, from_domains, to_mac, to_ips, to_domains, from_port, to_port, count, size

class Flow:
    def __init__(self, flow_data, timestamp):
        self.timestamp = timestamp
        from_data = flow_data["from"]
        if "mac" in from_data:
            self.from_mac = from_data["mac"]
        else:
            self.from_mac = ""
        self.from_ips = from_data["ips"]
        self.from_domains = from_data["domains"]
        to_data = flow_data["to"]
        if "mac" in to_data:
            self.to_mac = to_data["mac"]
        else:
            self.to_mac = ""
        self.to_ips = to_data["ips"]
        self.to_domains = to_data["domains"]
        self.from_port = flow_data["from_port"]
        self.to_port = flow_data["to_port"]
        self.count_out = flow_data["count"]
        self.size_out = flow_data["size"]
        self.count_in = 0
        self.size_in = 0

    def to_csv(self):
        return ",".join([
            str(self.timestamp),
            self.from_mac,
            "|".join(self.from_ips),
            "|".join(self.from_domains),
            self.to_mac,
            "|".join(self.to_ips),
            "|".join(self.to_domains),
            str(self.from_port),
            str(self.to_port),
            str(self.count_in),
            str(self.size_in),
            str(self.count_out),
            str(self.size_out)
        ])

    def to_simplified(self):
        # show only one domain name (if any); otherwise mac (if set),
        # otherwise one ip address.

        if len(self.from_domains) > 0:
            from_val = self.from_domains[0]
        elif self.from_mac != "":
            from_val = self.from_mac
        else:
            from_val = self.from_ips[0]

        if len(self.to_domains) > 0:
            to_val = self.to_domains[0]
        elif self.to_mac != "":
            to_val = self.to_mac
        else:
            to_val = self.to_ips[0]

        size_in = self.size_in
        if size_in > 1000000:
            size_in_str = "%dMb" % (size_in / 1000000)
        elif size_in > 1000:
            size_in_str = "%dkb" % (size_in / 1000)
        else:
            size_in_str = "%db" % size_in

        size_out = self.size_out
        if size_out > 1000000:
            size_out_str = "%dMb" % (size_out / 1000000)
        elif size_out > 1000:
            size_out_str = "%dkb" % (size_out / 1000)
        else:
            size_out_str = "%db" % size_out

        return "%s:%d  %s:%s  in: %s  out: %s packets: %d" % (from_val, self.from_port, to_val, self.to_port, size_in_str, size_out_str, self.count_in + self.count_out)

        #return ",".join([from_val, to_val,
        #    str(self.from_port),
        #    str(self.to_port),
        #    size_in_str,
        #    size_out_str
        #    ])

    def same_from(self, other):
        for ip in self.from_ips:
            if ip in other.from_ips:
                return True
        for domain in self.from_domains:
            if domain in other.from_domains:
                return True
        return False

    def same_to(self, other):
        for ip in self.to_ips:
            if ip in other.to_ips:
                return True
        for domain in self.to_domains:
            if domain in other.to_domains:
                return True
        return False

    def is_same(self, other):
        #if self.from_port != other.from_port:
        #    return False
        if self.to_port != other.to_port or self.from_port != other.from_port:
            return False
        # Okay, ports are the same, if mac is same, or they
        # have one shared ip or domain, consider them the same
        if self.from_mac == other.from_mac and self.to_mac == other.to_mac:
            return True
        return self.same_from(other) and self.same_to(other)

    def add(self, other):
        self.timestamp = other.timestamp
        self.count_out += other.count_out
        self.size_out += other.size_out
        self.count_in += other.count_in
        self.size_in += other.size_in

    def flip(self):
        self.from_mac, self.to_mac = self.to_mac, self.from_mac
        self.from_port, self.to_port = self.to_port, self.from_port
        self.from_ips, self.to_ips = self.to_ips, self.from_ips
        self.from_domains, self.to_domains = self.to_domains, self.from_domains
        self.count_in, self.count_out = self.count_out, self.count_in
        self.size_in, self.size_out = self.size_out, self.size_in

    def to_has_mac(self):
        return self.from_mac == "" and self.to_mac != ""

    def __str__(self):
        return str(self.data)

stored_flows = []

def store_traffic(data):
    global stored_flows
    if "command" in data and data["command"] == "traffic":
        #print("[XX] DATA TO CONVERT: '" + str(data) + "'")
        timestamp = data["result"]["timestamp"]
        for flow in data["result"]["flows"]:
            f = Flow(flow, timestamp)
            if f.to_has_mac():
                f.flip()
            #print(f.to_csv())
            new = True
            #print("[XX] NW: " + f.to_csv())
            for sf in stored_flows:
                #print("[XX] SF: " + sf.to_csv())
                if f.is_same(sf):
                    #print("ADD!")
                    sf.add(f)
                    new = False
                #else:
                #    print("[XX] same same")
                #    print("[XX] A: " + f.to_csv())
                #    print("[XX] B: " + sf.to_csv())
                #    print("[XX] , but different. but stil same.")
            if new:
                #print("APPEND!")
                stored_flows.append(f)

def on_message(client, userdata, msg):
    payload = msg.payload.decode("utf-8")
    #print(payload)
    store_traffic(json.loads(payload))

def main(args):
    global stored_flows
    client = mqtt.Client("TODO")
    client.connect(args.mqtt_host, args.mqtt_port)
    client.subscribe("SPIN/traffic")
    client.on_message = on_message
    client.loop_start()

    prev_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    while True:
        counter = 0
        while counter < args.interval * 10:
            time.sleep(0.1)
            counter += 1

        if len(stored_flows) > 0:
            stored_flows.sort(key=lambda e: e.size_in + e.size_out, reverse=True)
            dt = datetime.datetime.now()
            cur_time = dt.strftime("%Y-%m-%d %H:%M:%S")
            if not args.quiet:
                if args.clear_screen:
                    sys.stdout.write("\033[2J\033[H")
                print("Traffic summary %s - %s:" % (prev_time, cur_time))
            for f in stored_flows:
                if not args.quiet:
                    if args.show_csv:
                        print(f.to_csv())
                    else:
                        print(f.to_simplified())
            if args.writecsv:
                of_name = "%s_%s.csv" % (args.writecsv, dt.strftime("%Y-%m-%d_%H:%M:%S"))
                with open(of_name, "w") as outfile:
                    for f in stored_flows:
                        outfile.write("%s\n" % f.to_csv())
            if args.writesimplified:
                of_name = "%s_%s.csv" % (args.writesimplified, dt.strftime("%Y-%m-%d_%H:%M:%S"))
                with open(of_name, "w") as outfile:
                    outfile.write("Traffic summary %s - %s:\n" % (prev_time, cur_time))
                    for f in stored_flows:
                        outfile.write("%s\n" % f.to_simplified())
            stored_flows = []
            prev_time = cur_time

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('-m', '--mqtt-host', help='Connect to MQTT at the given host', default="127.0.0.1")
    parser.add_argument('-p', '--mqtt-port', help='Connect to MQTT at the given port (defaults to 1883)', type=int, default=1883)
    parser.add_argument('-i', '--interval', help='Collect and show/store summaries per interval seconds (defaults to 60)', type=int, default=60)
    parser.add_argument('-q', '--quiet', help='Do not print output to stdout', action="store_true")
    parser.add_argument('-r', '--clear-screen', help='Clear the terminal screen between intervals', action="store_true")
    parser.add_argument('-c', '--show-csv', help='Show full CSV output instead of the simplified overview', action="store_true")
    parser.add_argument('-w', '--writecsv', help='Save summaries as csv to outputfile-<date_time>.csv')
    parser.add_argument('-o', '--writesimplified', help='Save summaries in simplified format to outputfile-<date_time>.txt')

    args = parser.parse_args()

    main(args)
