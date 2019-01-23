### Overall repository structure

Some directories (such as src/tools/) will be subdivided further, with another directory for the relevant group of files (e.g. src/tools/pcap_reader/).

| Directory | Description |
| ------ | ----------- |
| /   | root directory |
| doc/ | Documentation |
| dist/    | Distribution-specific files and data |
| dist/debian/ | Debian-specific files and data
| dist/openwrt/ | OpenWRT-specific files and data
| scripts/ | General helper scripts for repository and building 
| src/ | Source code |
| src/spind/ | Main spin daemon source |
| src/web_ui | SPIN web API
| src/lib/ | shared (c) libraries
| src/lib_lua/ | shared (Lua) libraries
| src/include/ | shared (c) include files
| src/tools/ | SPIN tools (any language)
| src/tools_poc/ | SPIN tools in an early stage (c)
