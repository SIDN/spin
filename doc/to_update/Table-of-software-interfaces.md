# Spin software interfaces
The Spin software can be controlled by several interfaces. It is in rapid development so this list of interfaces is a snapshot.

The columns describe the availibility of the interfaces using various mechanisms.   
The WebAPI is a REST mechanism currently implemented by software listening to and aggregating traffic information published using MQTT.  
We are migrating to interfaces using either JSON-RPC or UBUS depending on the platform Spin is running on.  
Currently a lot of interfaces are implemented using a pseudo-rpc mechanism running over MQTT topics. This is planned to be obsoleted and migrated to real RPC's.  


Functional name | Tech name | WebAPI availibility | RPC/Ubus availability | MQTT
--- | --- | --- | --- | ---
Retreive devices and information | devicelist | yes | in 0.10 | no
Get profile of device | get_profile | yes | no | no
Change profile of device | set_profile | yes | no | no
??? | toggle_new | yes | no | no
Get notifications | get_notifications | yes | no |no
Set notification | set_notification | yes | no | no
Get profiles | get_profiles | yes | no | no
Add name of to node | add_name | no | no | yes
Get list of blocked nodes | get_blocks | no | no | yes
Add all IP addresses from node to list of blocked nodes | add_block_node | no | no | yes
Remove all IP addresses from node from list of blocked nodes | remove_block_node | no | no | yes
Remove IP address of blocked list | remove_block_ip | no | no | yes
Add all IP addresses from node to list of ignored nodes | add_ignore_node | no | no | yes
Remove all IP addresses from node from list of ignored nodes | remove_ignore_node | no | no | yes
Remove IP address of ignored list | remove_ignore_ip | no | no | yes
Add all IP addresses from node to list of allowed nodes | add_allow_node | no | no | yes
Remove all IP addresses from node from list of allowed nodes | remove_allow_node | no | no | yes
Remove IP address of allowed list | remove_allow_ip | no | no | yes
Reset list of ignored IP addresses to default | reset_ignores | no | no| yes
Block flow between two nodes | blockflow | no |  in 0.10 | no
Block flow between device and node|devblockflow|no|in 0.10|no
Get list of blocked flows | get_blockflow | no |  in 0.10| no
Get flows of device | get_deviceflow | yes | in 0.10 | no
Create node | create_node | yes | in 0.10 | no
Add IP address to node | add_ip_to_node | yes | in 0.10 | no

The WebAPI calls and MQTT calls  are documented in the file Spin-API.md
