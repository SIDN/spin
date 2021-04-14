# SPIN configuration

Both the collector (spind) and the web API (spinweb) can be configured through a single configuration file. A subset of the options can be provided on the command-line as well. For an overview of the command-line options, call either `spind -h` or `spinweb -h`. Command-line arguments take precedence over configuration file values.



## Configuration File

### Standard

The default configuration file for SPIN is `/etc/spin/spind.conf`. This can be overridden with the command-line option `-c`. A default configuration file can be printed to standard out with the command line `-C`.

### UCI

When compiled with UCI/UBUS support in OpenWRT, the configuration is provided through UCI and the static configuration file is ignored, as is the `-c` command-line argument.

Configuration settings in a UCI environment can be found in `/etc/config/spin`. The configurable items are the same as with a static configuration file, but the syntax is different, since this is now parsed by UCI.

For the UCI interface the package will be *spin* and the section *spind*. For the file interface the directory will be */etc/spin* and the file named *spind.conf*.


## Configurable items in Spind

|Configuration item|Description|Value type|Default|
|--|--|--|--|
|log_use_syslog| Use syslog instead of standard output logging | 0 or 1 | 1 |
|log_loglevel| The loglevel for logging | Integer | 6 |
|pubsub_host| The host name or IP address of the MQTT server (for spind to send traffic data, mqtt protocol) | String | 127.0.0.1 |
|pubsub_port| The port of the MQTT server (for spind to send traffic data, mqtt protocol) | Integer | 1883 |
|pubsub_websocket_host | The hostname of the MQTT server (for clients to read traffic data, websockets protocol) | String | 127.0.0.1 |
|pubsub_websocket_port | The port of the MQTT server (for clients to read traffic data, websockets protocol) | Integer | 1884 |
|pubsub_timeout | The time-out value for MQTT connections in seconds | Integer | 60 |
|pubsub_run_mosquitto | If set to 1, spind will start a mosquitto instance with the settings as used in the SPIN configuration, instead of connecting to an existing one | 0 or 1 | 1 |
|pubsub_run_password_file | Filename of a standard password file to use for the mosquitto instance that is started if pubsub_run_mosquitto is set to 1. This requires users to authenticate to mosquitto in order to be able to read traffic data | String ||
|OBSOLETE? iptable_queue_dns|The Iptables queue number for DNS data, change this if the queue number is already used by some other program|Integer|1|
|iptable_queue_block|The Iptables queue number for block rule data, change this if the queue number is already used by some other program|Integer|2|
|OBSOLETE?iptable_place_dns|
|iptable_debug|Log iptables commands to the given file, for debugging purposes|String|/tmp/block_commands|
|node_cache_retain_time|The time (in seconds) to keep nodes (devices, remote addresses) in memory after they were last seen to send or receive traffic|Integer|1800|
|dots_enabled|Enable the experimental DOTS implementation|0 or 1|0|
|dots_log_only|Only log DOTS notifications, do not act on them|0 or 1|0|
|spinweb_interfaces|A comma-separated list of IP addresses on which spinweb listens for requests|String|127.0.0.1|
|spinweb_port|The port that spinweb listens on for requests|Integer|13026|
|spinweb_tls_certificate_file|The PEM-formatted certificate file for spinweb TLS connections. If this (and spinweb_tls_key_file) is specified, spinweb uses https instead of http to serve requests. When TLS is configured, mqtt websockets connections are automatically assumed to use wss:// instead of ws://, so when an MQTT server is run independently from SPIN, it should be configured with TLS support as well.|String||
|spinweb_tls_certificate_file|The private key file for spinweb TLS connections. See _spinweb_tls_certificate_file_|String||
|spinweb_password_file| Filename of a standard password file to use to access the spinweb HTTP pages. Setting a value here enables HTTP authentication. Note that if pubsub_run_password_file is set, users will have to authenticate twice (once to get to the page, once to access traffic data), so if mosquitto is protected by a password file, it may not be necessary to set one here as well | String ||

## Example configuration file (default)

	log_usesyslog = 1
	log_loglevel = 6
	pubsub_host = 127.0.0.1
	pubsub_port = 1883
	pubsub_websocket_host = 127.0.0.1
	pubsub_websocket_port = 1884
	pubsub_channel_traffic = SPIN/traffic
	pubsub_timeout = 60
	pubsub_run_mosquitto = 1
	pubsub_run_password_file = 
	iptable_queue_dns = 1
	iptable_queue_block = 2
	iptable_place_dns = 0
	iptable_place_block = 0
	iptable_debug = /tmp/block_commands
	node_cache_retain_time = 1800
	dots_enabled = 0
	dots_log_only = 0
	spinweb_interfaces = 127.0.0.1
	spinweb_port = 13026
	spinweb_tls_certificate_file = 
	spinweb_tls_key_file = 
	spinweb_password_file = 

## Example configuration file (UCI)

	config spind 'spind'
	    option iptable_debug '/tmp/blockcommands'
	    option iptable_queue_dns '1'
	    option iptable_queue_block '2'
	    option iptable_place_dns '0'
	    option iptable_place_block '0'
	    option pubsub_host '127.0.0.1'
	    option pubsub_port '1883'
	    option pubsub_channel_traffic 'SPIN/traffic'
	    option pubsub_timeout '60'
	    option log_usesyslog '1'
	    option log_loglevel '1'
	    option spinweb_interfaces '127.0.0.1, 192.168.11.1'
	    option pubsub_run_mosquitto '1'
	    option spinweb_tls_certificate_file '/etc/nginx/nginx.cer'
	    option spinweb_tls_key_file '/etc/nginx/nginx.key'

### Configuration code in spind

(this section should go to code)

In the code there is a source file config_common.c containing the generic option handling, including a table with all options and the defaults for each option. For each option there is an entry point to get the value, something like:

int spindconf_pubsub_port() { return(appropriate value); }

This code calls initialization code for either UCI or a simple file, depending on the platform on which it runs. The selection for the method is platform specific, and will be done automatically at compile-time. If a UCI library is available the UCI method will be used, if not then the file method is used.

No other code in the program needs to be changed if another configuration method is added.