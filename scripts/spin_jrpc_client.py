#!/usr/bin/env python3

#
# Very basic json-rpc client for SPIN WEB API
#

# Usage: spin_jrpc_client.py <command> [parameter]

import argparse
import json
import sys
import random
import requests

SPIN_WEB_URI = "192.168.8.1:8080/spin_api/jsonrpc"


class JsonRPCClient(object):
    def __init__(self, command, params, uri, verbose=False):
        self.command = command
        self.params = params
        self.uri = uri
        self.verbose = verbose

    def vprint(self, msg):
        if self.verbose:
            print(msg)

    def build_json_command(self):
        result = {
            'json_rpc': '2.0',
            'id': random.randint(1, 65535),
            'method': self.command
        }
        # Parameters come in two's:
        # <parameter name> <parameter value>
        if self.params:
            result['params'] = {}
            for i in range(0, len(self.params), 2):
                # value may be a json value, but we'll quote it if it
                # is a string
                try:
                    param_value = json.loads(self.params[i+1])
                except json.decoder.JSONDecodeError:
                    param_value = json.loads('"%s"' % self.params[i+1])
                result['params'][self.params[i]] = param_value
        return result

    def send(self, json_cmd):
        response = requests.post(url = self.uri, json=json_cmd)
        self.vprint("Return code: %d" % response.status_code)
        self.vprint("Raw response content: %s" % response.content.decode("utf-8"))
        return response.json()

    def run(self):
        json_cmd = self.build_json_command()
        self.vprint("JSON Command: %s" % json_cmd)
        result = self.send(json_cmd)
        # TODO: check id?
        if 'error' in result:
            print("Error from server!")
            print("Error code: %d" % result['error']['code'])
            print("Error message: %s" % result['error']['message'])
        else:
            print(json.dumps(result, indent=2))

if __name__ == '__main__':
    arg_parser = argparse.ArgumentParser(prog="spin_jrpc_client.py")
    arg_parser.add_argument('-u', '--uri', default='http://192.168.8.1/spin_api/jsonrpc',
                            help='base URI of the JSON-RPC web api endpoint')
    arg_parser.add_argument('-v', '--verbose', action="store_true", help="be verbose")
    arg_parser.add_argument('command', help='name of the rpc command')
    arg_parser.add_argument('params', nargs='*', help='command parameters; name, value pairs as separate arguments')

    args = arg_parser.parse_args()

    if len(args.params) % 2 != 0:
        sys.stderr.write("Error: method parameters must be parameter_name, parameter value pairs, as separate arguments\n")
        sys.exit(1)

    client = JsonRPCClient(args.command, args.params, args.uri, args.verbose)
    client.run()
