#!/usr/bin/env python3

# Copyright SIDN Labs 2019
# License: GPL, see the LICENSE file for more information

#
# This is a small script that creates DOTS signal home mitigation requests
#

import argparse
import ipaddress
import json
import requests
import sys

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

    def add_target_prefix(self, prefix):
        self.target_prefixes.append(Prefix(prefix))

    def add_source_prefix(self, prefix):
        self.source_prefixes.append(Prefix(prefix))

    def set_lifetime(self, lifetime):
        self.lifetime = lifetime

    def as_obj(self):
        scope = {
            "lifetime": self.lifetime
        }

        if self.source_prefixes:
            scope['ietf-dots-call-home:source-prefix'] = [str(p) for p in self.source_prefixes]

        if self.target_prefixes:
            scope['target-prefix'] = [str(p) for p in self.target_prefixes]

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
                print("[XX] RESPONSE: '%s'" % response.content)
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

    if args.print:
        print(json.dumps(mr.as_obj(), indent=2))
    if args.apply:
        sender = RPCSender(url=args.url)
        result = sender.send(mr)
        if result:
            print(result)

if __name__=='__main__':
    parser = argparse.ArgumentParser(description='Process some integers.')
    parser.add_argument('-s', '--source', type=str, action='append', default=[], help='Source prefix (can be used multiple times)')
    parser.add_argument('-t', '--target', type=str, action='append', default=[], help='Target prefix (can be used multiple times)')
    parser.add_argument('-p', '--print', action="store_true", help="Print the request as json")
    parser.add_argument('-a', '--apply', action="store_true", help="Send the command to the SPIN JSON-RPC API")
    parser.add_argument('-u', '--url', help="Use the given URL for -a. Default: http://192.168.8.1/spin_api/jsonrpc")

    args = parser.parse_args()
    main(args)
