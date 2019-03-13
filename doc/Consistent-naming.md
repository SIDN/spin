# Standard naming in Spind

The three lists are now officially named:
BLOCK, IGNORE and ALLOW

the old names FILTER and EXCEPT are deprecated

##  Spind code
The old names still occur, but only in the pubsub interface where they are used on the Mosquiito channels

get_filters"     PSC_V_GET,      PSC_O_IGNORE},
get_blocks    PSC_V_GET,      PSC_O_BLOCK},
get_alloweds     PSC_V_GET,      PSC_O_ALLOW},
get_names       PSC_V_GET,      PSC_O_NAME },
add_filter      PSC_V_ADD,      PSC_O_IGNORE}, // Backw
add_filter_node  PSC_V_ADD,      PSC_O_IGNORE},
add_name        PSC_V_ADD,      PSC_O_NAME},
add_block_node   PSC_V_ADD,      PSC_O_BLOCK},
add_allow_node  PSC_V_ADD,      PSC_O_ALLOW},
remove_filter   PSC_V_REM_IP,   PSC_O_IGNORE}, // Backw
remove_filter_node PSC_V_REM,      PSC_O_IGNORE},
remove_filter_ip PSC_V_REM_IP,   PSC_O_IGNORE},
remove_block_node PSC_V_REM,      PSC_O_BLOCK},
remove_block_ip PSC_V_REM_IP,   PSC_O_BLOCK},
remove_allow_nodePSC_V_REM,      PSC_O_ALLOW},
remove_allow_ip  PSC_V_REM_IP,   PSC_O_ALLOW},
reset_filters   PSC_V_RESET,    PSC_O_IGNORE},

Here there is also a discrepancy where filter means filter_ip in remove, but node in add

Proposal:

get_blocks
get_ignores
get_alloweds
get_names

add_block_node
add_ignore_node
add_allow_node
add_name

remove_block_node
remove_ignore_node
remove_allow_node

remove_block_ip
remove_ignore_ip
remove_allow_ip

reset_ignores (TODO, seems unimplemented at the moment)

These changes need to be made to all Mosquitto talkers

Furthermore in the bubble-app UI the word filter still occurs
