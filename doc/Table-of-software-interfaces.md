# Spin software interfaces
The Spin software can be controlled by several interfaces. It is in rapid development so this list of interfaces is a snapshot.

The columns describe the availibility of the interfaces using various mechanisms.   
The WebAPI is a REST mechanism currently implemented by software listening to and aggregating traffic information published using MQTT.  
We are migrating to interfaces using either JSON-RPC or UBUS depending on the platform Spin is running on.  
Currently a lot of interfaces are implemented using a pseudo-rpc mechanism running over MQTT topics. This is planned to be obsoleted and migrated to real RPC's.  


Functional name | Tech name | WebAPI availibility | RPC/Ubus availability | MQTT
--- | --- | --- | --- | ---
Retreive devices and information | get_devices | yes | planned for 0.10 | no
Get profile of device | get_profile | yes | no | no
Change profile of device | set_profile | yes | no | no
??? | toggle_new | yes | no | no
Add name of to node | add_name | no | planned for 0.10 | yes
Get list of blocked nodes | get_blocks | no | planned for 0.10 | yes
Add all IP addresses from node to list of blocked nodes | add_block_node | no | planned for 0.10 | yes
Remove all IP addresses from node from list of blocked nodes | remove_block_node | no | planned for 0.10 | yes
Remove IP address of blocked list | remove_block_ip | no | planned for 0.10 | yes
ditto | ditto | no | planned for 0.10 | yes
Reset list of ignored IP addresses to default | reset_ignores | no | planned for 0.10| yes
Block flow between two nodes | blockflow | no | Ubus in beta | no
Get list of blocked flows | get_blockflow | no | Ubus in beta| no

The WebAPI calls and MQTT calls  are documented in the file Spin-API.md
