# Changelog

## Upcoming
* Nothing yet

## [0.11] (2020-01-29)
* Added '-c' option to spind for specifying a configuration file
* Added '-e' option to spind for specifying an external traffic data input socket
* Added experimental DOTS signal message processing (disabled by default)
* Added small command-line tool to send DOTS signal messages
* Device flow data now includes port numbers and ICMP types
* Fixed an issue where spind would crash if the logfile can't be opened
* Fixed an issue where clicking on empty space in the Web UI showed an error
* Fixed an issue where traffic captures would time out


## [0.10] (2019-11-07)
* The MQTT Traffic channel protocol has changed; node information is now sent in a separate subchannel,
and flow information uses node id's instead of the full node data
* Added RPC functionality: some information and functionality can now directly be accessed. If UBUS is
available, SPIN uses that. Otherwise, it will listen for JSON-RPC commands on /var/run/spin_rpc.sock.
An overview of the RPC methods can be requested by the RPC method 'list_rpc_methods'
* The web API now provides and endpoint for all RPC methods as JSON-RPC
* The SPIN/commands channel is no longer used for interactive commands, and all functionality in spind
handling commands here has been replaced by the RPC mechanism
* The bubble app now uses the web API rpc endpoint for commands and direct information retrieval (except
traffic data). This improves performance and reliability, but it does mean that both spind and
spin_webui must be running.
* 'Most recent flows' information can now be retrieved for devices on the network
* SPIN (and the bubble app) now provide functionality to block traffic between two specific nodes (in
addition to the existing 'all traffic from and to one node').
* Added an 'extsrc' source, where pcap data can be sent to SPIN directly
* The 'mqtt_host' on the bubble app now defaults to the host of the app itself
* Bugfixes and refactoring: see the git repository for details

## [0.9] (2019-05-02)

* Removed kernel module and replaced with conntrack/nflog/nfqueue implementation
* Added internal module registration and callback architecture
* Made (mqtt) commands more consistent (removed 'filter' and 'except')
* Internal node cache is now regularly cleaned
* Added SPIN configuration support (direct and with UCI)
* Added operational statistics (published in mqtt SPIN/stat channel)
* Added initial version of responsive SPA front-end (http://valibox./spin)
* Added very early profile concept
* Added PoC-tool 'pcap-reader'
* Added PoC-tool 'peak-detction'
* Fixed a memory leak
* Small updates and bugfixes in visualiser

## [0.8-beta] (2019-01-31)

* Web UI now uses lua-minittp
* Added 'Download PCAP traffic' option to the bubble app, you can directly run tcpdump from the web interface now.
* Added 'protocol' field to mqtt traffic format
* Added a web API for configuration and control, see https://github.com/SIDN/spin/blob/master/doc/web_api.md
* From the WEB API, there is a very rudimentary option to control firewall rules through profiles (as a stepping stone to MUD which is planned for the next release)

## [0.7] (2018-08-08)

* Web UI now uses lua-minittp
* Added 'Download PCAP traffic' option to the bubble app, you can directly run tcpdump from the web interface now.
* Added 'protocol' field to mqtt traffic format
* Added a web API for configuration and control, see https://github.com/SIDN/spin/blob/master/doc/web_api.md
* From the WEB API, there is a very rudimentary option to control firewall rules through profiles (as a stepping stone to MUD which is planned for the next release)

## [0.6] (2018-04-10)

* Added DNS query logging / visualisation
* Big efficiency update in kmod/spind communication, which should result in much less 'missed' packets
* The location of the MQTT server is now flexible in all tools and daemons
* Fixed color of bubbles and arrows (#27, #29, #31)
* Fixed block and interface buttons (#28, #35)
* Fixed valibox interface when using the IP address in the browser (#32)
* Added command-line option to set/unset features of spin_enforcer
* Updated Vis library
* Added early MUD prototype
* Added early prototype of Provider API (in spin_enforcer and incident_report_listener)
* Improved node merging


## [0.5] (2017-10-16)

* Renamed main spin daemon spin_mqtt to spind
* Added 'block' and 'allow' functionality to SPIN graph front-end
* Added experimental 'auto block' tool spin_enforcer
* Added verbosity option to capture module
* Added 'local' mode option to capture module (use IN/OUT chains only, not FORWARD)
* Improvements in capture module
* Fixed issue where ignoring a node did not always remove all relevant other nodes from view
* Fixed issue where user-set name was not shown until restart
* Fixed issue where ARP table was not always read completely
