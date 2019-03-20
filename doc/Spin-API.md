# SPIN generic API

Description of all entry points into SPIN. Not currently all to the same endpoint.

All entry points get a list(0+) of arguments and return a list(0+) of results.

The names are currently suggestions to be discussed.

## web_ui entry points

	get_devices
		args: NULL
		result: dictionary, indexed by mac-address, containing:
			appliedProfiles: list of profile identifiers
			enforcement: <unused>
			lastSeen: timestamp
			logging: <unused>
			mac: mac-address (same as index)
			name: user set name
			new: true if no profile applied

***
	
	get_profile
		args: mac-address
		result: list of profile identifiers
		
***
	set_profile
		args: mac-address, profile identifier
		result: NULL
		
***
	toggle_new
		args: mac-address
		result: NULL
		
***
	get_notifications
		args: NULL
		result: list of dictionaries(structs) containing:
			id (integer): the unique identifier of the message
			message (string): the message to show to the user
			messageKey (string): a unique message key that can be used for i18n
			messageArgs (list of strings): variable data arguments that may be used in the message (such as names)
			timestamp (integer): A UNIX timestamp (seconds since epoch), set to the time the message was created.
			deviceMac (string, optional): The MAC address of the device this notification refers to
			deviceName (string, optional): The name of the device this notification refers to, see /spin_api/devices for information on what value is used for the name
			
***
	set_notification
		args: message
***
	delete_notification
		args: notification-id
***
	get_profiles
		args: NULL
		result: list of dictionaries(structs) containing:
			id (string): A unique identifier for the profile
			name (string): A human-readable name for the profile
			description (string): A description of the profile
			type (string): One of CREATED_BU_USER, or CREATED_BY_SYSTEM

## Spind entry points

	add_name
		args: node-id, name
		result: NULL
***
	get_blocks
		args: NULL
		result: list of blocked IP addresses
		
***
Same for ignores and alloweds
***
	add_block_node
		args: node-id
		result: list of blocked IP addresses
***
Same for ignores and alloweds
***
	remove_block_node
		args: node-id
		result: list of blocked IP addresses
***
Same for ignores and alloweds
***
	remove_block_ip
	Tuesday, 19. March 2019 12:46PM 
		args: ip-address
		result: list of blocked IP addresses
***
Same for ignores and alloweds
***
	reset_ignores
	Tuesday, 19. March 2019 12:48PM 
		args: NULL
		result: list of ignored IP addresses


