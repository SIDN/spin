# DOTS signal home handler  prototype

## Introduction

SPIN now includes a proof of concept for handling DOTS signal home mitigation requests, based on the IETF draft [https://tools.ietf.org/html/draft-ietf-dots-signal-call-home-07](https://tools.ietf.org/html/draft-ietf-dots-signal-call-home-07).

## DOTS mitigation requests handling

SPIN does not include a DOTS server, which receives and handles DOTS requests from for instance an ISP. SPIN can, however, process mitigation requests directly, through the JSON-RPC (web) API.

When DOTS support is enabled, and a mitigation request is received, SPIN will check its recent device history to see if there are any matches to the traffic specified in the mitigation request. If a device is found to match, and SPIN is configured to act upon mitigation requests, the device will be put in quarantine. SPIN will block all traffic to and from this device, until the user unblocks it again.

## Limitations of the current prototype

- SPIN only checks (recent) history of the devices, it does not remember mitigation requests for future traffic
- SPIN only checks the target information (addresses, port ranges, and icmp types), apart from icmp types, it ignore source data

## Enabling the DOTS handling in SPIN

By default, SPIN does not handle DOTS mitigation requests, as this is an early prototype. You can enable SPIN's processing of DOTS, and what to do when it finds a match, in spind.conf.

There are two options for DOTS processing:

    dots_enabled = true|false

This enables the processing of DOTS requests in the RPC subsystem.

    dots_log_only = true|false

This controls the behaviour when a device is found that matches the mitigation request. When this configuration option is set to true, SPIN will only log the match, but will not act on it. When this option is set to false, it will quarantine the device, disallowing any traffic to and from the device.


## Constructing and sending DOTS Requests

We have added a small script to the `scripts/` directory of SPIN, called `dots_request.py`. With it, you can create a DOTS signal phone home mitigation request.

    > ./dots_request.py -h
    usage: dots_request.py [-h] [-s SOURCE] [-t TARGET] [-p] [-a] [-u URL]

    Create a DOTS signal phone home mitigation request.

    optional arguments:
      -h, --help            show this help message and exit
      -s SOURCE, --source SOURCE
                            Source prefix (can be used multiple times)
      -t TARGET, --target TARGET
                            Target prefix (can be used multiple times)
      -p, --print           Print the request as json
      -a, --apply           Send the command to the SPIN JSON-RPC API
      -u URL, --url URL     Use the given URL for -a. Default:
                            http://192.168.8.1/spin_api/jsonrpc
      --port PORT           Target port range (can be used multiple times)
      --icmp ICMP           ICMP Type range (can be used multiple times)

You can generate and print a request with the `-p` option:

	> ./dots_request.py -s 192.0.1.1 -t 192.0.1.2 -p
	{
	  "ietf-dots-signal-channel:mitigation-scope": {
	    "scope": [
	      {
		"lifetime": 3600,
		"ietf-dots-call-home:source-prefix": [
		  "192.0.1.1/32"
		],
		"target-prefix": [
		  "192.0.1.2/32"
		]
	      }
	    ]
	  }
	}

With the `-a` flag, you can send it to SPIN. Use `-u` to use a different JSON-RPC web api, if necessary.

	> ./dots_request.py -s 192.0.1.1 -t 192.0.1.2 -a
	{'jsonrpc': '2.0', 'id': 228254}

The current version only reports success when the request itself it successfully processed, it does not provide feedback on whether there is a match and whether it was mitigated, nor does it implement signal challenge responses with mid values.

When DOTS support is *not* enabled, sending a DOTS request will result in an error:

	> ./dots_request.py -s 192.0.1.1 -t 192.0.1.2 -a
	{'id': 696326, 'jsonrpc': '2.0', 'error': {'message': 'DOTS support is not enabled in spind.conf', 'code': 1}}
