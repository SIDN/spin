# Configuration of Spind

The spind program in the collection of Spin programs is the central part doing all the communication. It needs to know various things in order to start up that can be different on different platforms. To be able to consistently configure this we made it so that it can be configured using the method appropriate on each platform.

Currently we have two configuration methods, but using a structure in the code that will make it easy to add other methods.
On OpenWRT and related systems the UCI method will be used.

At the moment we only use static configuration, so the program reads the configuration at startup, initialises itself and starts running. If changes are made to the configuration the program must then be restarted.

The structure of the code makes it possible to use dynamic configuration at a later stage, where it would be possible to change configuration items on the fly. We are already contemplating this for the logging process(increase/decrease loglevel on the fly).

The code contains a default value for each option, which is used if the configuration method does not set the option.

## Configurable items in Spind

Currently these are:

- the configuration for the calls to the iptables command, stating the queues used, where the iptables commands are inserted and if debugging takes place and where.

- The configuration of the Mosquitto interface

- The configuration of logging

Others will undoubtedly be added.

## General structure

All Spin configuration is divided in sections, one for each program. Currently only Spind uses this but others will probably be added.
For the UCI interface(see below) the package will be *spin* and the section *spind*. For the file interface the directory will be */etc/spin* and the file named *spind.conf*.

## UCI structure

Package *spin*, initialized from the file */etc/config/spin*.

### Variables(as they are now)
config spind 'spind'

	option iptable_debug '/tmp/blockcommands'
	option iptable_queue_dns '1'
	option iptable_queue_block '2'
	option iptable_place_dns '0'
	option iptable_place_block '0'
	option pubsub_host '127.0.0.1'
	option pubsub_port '1883'
	option pubsub_channel_commands 'SPIN/commands'
	option pubsub_channel_traffic 'SPIN/traffic'
	option pubsub_timeout '60'
	option log_usesyslog '1'
	option log_loglevel '6'
	
The values shown are the default in the current code.


### The file method
Make file */etc/spin/spind.conf*, with exactly the same names and values, so for example

	#
	# Config of Spind
	#
	
	pubsub_port=1883

### Configuration code in spind

In the code there is a source file config_common.c containing the generic option handling, including a table with all options and the defaults for each option. For each option there is an entry point to get the value, something like:

int spindconf_pubsub_port() { return(appropriate value); }

This code calls initialization code for either UCI or a simple file, depending on the platform on which it runs. The selection for the method is platform specific, and will be done automatically at compile-time. If a UCI library is available the UCI method will be used, if not then the file method is used.

No other code in the program needs to be changed if another configuration method is added.