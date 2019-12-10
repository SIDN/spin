#!/usr/bin/env python3

# Copyright SIDN Labs 2019
# License: GPL, see the LICENSE file for more information

#
# This is a small script that creates DOTS signal home mitigation requests
#

import argparse
import ipaddress
import json
import re
import requests
import sys


port_range_matcher = re.compile("^([0-9]+)-([0-9]+)$")
def parse_port_range(port_str):
    """one of "80" or "80-89" """
    match = port_range_matcher.match(port_str)
    if match:
        return {
            "lower-port": int(match.group(1)),
            "upper-port": int(match.group(2)),
        }
    else:
        try:
            return {
                "upper-port": int(port_str)
            }
        except ValueError:
            raise ValueError("Invalid port range string, should be <number>[-<number>]")
    

class Prefix(object):
    def __init__(self, prefix_str):
        self.prefix = ipaddress.ip_network(prefix_str)

    def __str__(self):
        return str(self.prefix)

class MitigationRequest(object):
    def __init__(self):
        self.lifetime = 3600
        self.target_prefixes = []
        self.source_prefixes = []
        self.ports = []

    def add_target_prefix(self, prefix):
        self.target_prefixes.append(Prefix(prefix))

    def add_source_prefix(self, prefix):
        self.source_prefixes.append(Prefix(prefix))

    def set_lifetime(self, lifetime):
        self.lifetime = lifetime

    def add_port_range(self, port_str):
        """port_str can be a single port number ("80", or a range in the
           form "80-89"
        """
        self.ports.append(parse_port_range(port_str))

    def as_obj(self):
        scope = {
            "lifetime": self.lifetime
        }

        if self.source_prefixes:
            scope['ietf-dots-call-home:source-prefix'] = [str(p) for p in self.source_prefixes]

        if self.target_prefixes:
            scope['target-prefix'] = [str(p) for p in self.target_prefixes]

        if self.ports:
            scope['target-port-range'] = self.ports

        result = {
            "ietf-dots-signal-channel:mitigation-scope": {
                "scope": [ scope ]
            }
        }
        return result

class RPCSender(object):
    """
    Creates and sends a JSON-RPC request to a SPIN device, containing
    a MitigationRequest
    """
    def __init__(self, url=None):
        if url is None:
            self.url = "http://192.168.8.1/spin_api/jsonrpc"
        else:
            self.url = url

    def create_jsonrpc_request(self, mitigation_request):
        result = {
            'method': 'dots_signal',
            'params': {
                'dots_signal': mitigation_request.as_obj()
            }
        }

        return result

    def send(self, mitigation_request):
        headers = {
            'Content-Type': 'application/json',
            'Accept': 'application/json'
        }
        data = self.create_jsonrpc_request(mitigation_request)
        response = requests.post(self.url, data=json.dumps(data), headers=headers)
        if response.content and response.content != "":
            try:
                return response.json()
            except Exception as exc:
                sys.stderr.write("Server response was not JSON: %s\n" % str(exc))
                sys.stderr.write("Server response: %s\n" % response.content.decode('utf-8'))
                return None
        else:
            return None

def main(args):
    mr = MitigationRequest()
    if args.source:
        for prefix in args.source:
            try:
                mr.add_source_prefix(prefix)
            except ValueError as ve:
                print("Bad prefix: %s" % str(ve))
                sys.exit(1)
    if args.target:
        for prefix in args.target:
            try:
                mr.add_target_prefix(prefix)
            except ValueError as ve:
                print("Bad prefix: %s" % str(ve))
                sys.exit(1)

    if args.port:
        for port_str in args.port:
            mr.add_port_range(port_str)

    if args.print:
        print(json.dumps(mr.as_obj(), indent=2))
    if args.apply:
        sender = RPCSender(url=args.url)
        result = sender.send(mr)
        if result:
            print(result)

if __name__=='__main__':
    parser = argparse.ArgumentParser(description='Create a DOTS signal phone home mitigation request.')
    parser.add_argument('-s', '--source', type=str, action='append', default=[], help='Source prefix (can be used multiple times)')
    parser.add_argument('-t', '--target', type=str, action='append', default=[], help='Target prefix (can be used multiple times)')
    parser.add_argument('-p', '--print', action="store_true", help="Print the request as json")
    parser.add_argument('-a', '--apply', action="store_true", help="Send the command to the SPIN JSON-RPC API")
    parser.add_argument('-u', '--url', help="Use the given URL for -a. Default: http://192.168.8.1/spin_api/jsonrpc")
    parser.add_argument('--port', type=str, action='append', default=[], help="Target port range (can be used multiple times)")
    args = parser.parse_args()

    main(args)
