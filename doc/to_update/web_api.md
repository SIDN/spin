# Web API

Note: this is the old API which is no longer in use; Most functionality
has been replaced with a JSON-RPC mechanism, which is served

The spin_webui daemon offers some web pages, and a REST API that can read and control several aspects of the SPIN software.

- [JSON API Endpoints](#json-api-endpoints)
  - [/spin_api/devices](#spin_apidevices)
    - [Example response](#example-response)
  - [/spin_api/devices/\[mac-address\]/appliedProfiles](#spin_apidevicesmac-addressappliedprofiles)
    - [Example request content (POST):](#example-request-content-post)
    - [Example response (GET)](#example-response-get)
  - [/spin_api/devices/\[mac-address\]/toggleNew](#spin_apidevicesmac-addresstogglenew)
  - [/spin_api/notifications](#spin_apinotifications)
    - [Example response](#example-response)
  - [/spin_api/notifications/create](#spin_apinotificationscreate)
    - [Example request content:](#example-request-content)
  - [/spin_api/notifications/\[integer\]/delete](#spin_apinotificationsintegerdelete)
  - [/spin_api/profiles](#spin_apiprofiles)
    - [Example response](#example-response)
  - [/spin_api/configuration](#spin_apiconfiguration)
- [Other URLs](#other-urls)
  - [/spin_api/tcpdump](#spin_apitcpdump)
  - [/spin_api/tcpdump_status](#spin_apitcpdump_status)
  - [/spin_api/tcpdump_start](#spin_apitcpdump_start)
  - [/spin_api/tcpdump_stop](#spin_apitcpdump_stop)


## JSON API Endpoints

All JSON API endpoints return either a direct JSON data structure as a response, or an error in the form

    {
      "status": [HTTP status code] (integer)
      "error": [Error message] (string)
    }

For the endpoints that accept content parameters, they can be sent as either JSON (Content-Type application/json) or x-www-form-urlencoded (Content-Type application/x-www-form-urlencoded).


### /spin_api/devices

**Method(s)**: GET

**URL Parameters**: None

**Content Parameters**: None


Returns a JSON dictionary containing the devices SPIN has seen since its startup, and some information about the devices.

The dictionary is indexed by the MAC address of the device, and each entry is a dictionary with the following values:

* **appliedProfiles**: a list of profile identifiers that are currently set for this device, see /spin_api/profiles. Note that currently, only 1 profile can be set
* **enforcement**: currently unused
* **lastSeen**: a UNIX timestamp (seconds since epoch) of when the device was last active (either sent or received data)
* **logging**: currently unused
* **mac**: the MAC address of the device
* **name**: the name of the device. If the user has set a name, this name is shown. Otherwise, if the device has a name through DHCP, that name is shown. If none of those are true, the first IP address of this device that was seen is used.
* **new**: If no profile was set for this device yet, this value is set to true. False otherwise.

#### Example response

    {
      "3d:a3:33:12:aa:4f": {
        "appliedProfiles": [
          "allow_all"
        ],
        "enforcement": "",
        "lastSeen": 1530706372,
        "logging": "",
        "mac": "3d:a3:33:12:aa:4f",
        "name": "laptop",
        "new": true
      },
      "b7:44:ec:cc:12:3d": {
        "appliedProfiles": [
          "deny_all"
        ],
        "enforcement": "",
        "lastSeen": 1530706412,
        "logging": "",
        "mac": "b7:44:ec:cc:12:3d",
        "name": "macbook-11223123",
        "new": false
      }
    }

### /spin_api/devices/\[mac-address\]/appliedProfiles

**Method(s)**: GET, POST

**URL Parameters**:
  * mac-address: The MAC address of the device, of the form aa:bb:cc:dd:ee:ff

**Content Parameters \[POST only\]**:
  * profile_id (string): The profile to set

For GET, returns a list of the device profiles identifiers that were set for this device. Note that currently, only 1 profile can be set.

For POST, sets the given profile for the given device.

#### Example request content (POST):

    { "profile_id": "deny_all" }

#### Example response (GET)

    ["allow_all"]

### /spin_api/devices/\[mac-address\]/toggleNew

**Method(s)**: POST

**URL Parameters**:
  * mac-address: The MAC address of the device, of the form aa:bb:cc:dd:ee:ff

**Content Parameters**: None

Toggles the 'new' status of the device.

### /spin_api/notifications

**Method(s)**: GET

**URL Parameters**: None

**Content Parameters**: None


Returns a list of notifications from SPIN to the user, in the form of a list of dictionaries. The dictionaries contain the following fields:

* id (integer): the unique identifier of the message
* message (string): the message to show to the user
* messageKey (string): a unique message key that can be used for i18n
* messageArgs (list of strings): variable data arguments that may be used in the message (such as names)
* timestamp (integer): A UNIX timestamp (seconds since epoch), set to the time the message was created.
* deviceMac (string, optional): The MAC address of the device this notification refers to
* deviceName (string, optional): The name of the device this notification refers to, see /spin_api/devices for information on what value is used for the name

Current messageKeys, their default text values, and their arguments are:

| messageKey     | Arguments      | Default text                                |
|----------------|----------------|---------------------------------------------|
| new_device     |         -      | New device on network! Please set a profile |
| profile_set_to |  profile-name  | Profile set to <arg 0>                      |
| profile_error  |  error-message | Error setting profile: <arg 0>              |

#### Example response

    [
       {
          "deviceName" : "raptor",
          "deviceMac" : "1a:5f:41:3f:fc:e6",
          "messageArgs" : [],
          "timestamp" : 1532526169,
          "messageKey" : "new_device",
          "id" : 1,
          "message" : "New device on network! Please set a profile"
       },
       {
          "deviceName" : "android-33535267",
          "id" : 2,
          "messageKey" : "new_device",
          "message" : "New device on network! Please set a profile",
          "messageArgs" : [],
          "timestamp" : 1532526173,
          "deviceMac" : "84:cf:bf:44:44:44"
       },
       {
          "messageArgs" : [
             "IPv4 only"
          ],
          "deviceMac" : "84:cf:bf:44:44:44",
          "timestamp" : 1532526223,
          "messageKey" : "profile_set_to",
          "id" : 3,
          "message" : "Profile set to IPv4 only",
          "deviceName" : "android-33535267"
       }
    ]


### /spin_api/notifications/create

**Method(s)**: POST

**URL Parameters**: None

**Content Parameters**:
  * message (string): A message for the user

Adds the given message to the list of notifications for the user

#### Example request content:

    { "message": "A system update is available for download" }

### /spin_api/notifications/\[integer\]/delete

**Method(s)**: POST

**URL Parameters**: notification identifier (integer)

**Content Parameters**: None

Deletes the notification with the given identifier


### /spin_api/profiles

**Method(s)**: GET

**URL Parameters**: None

**Content Parameters**: None

Returns the available profiles for devices, in the form of a list of dictionaries. The dictionaries contain the following fields:

* id (string): A unique identifier for the profile
* name (string): A human-readable name for the profile
* description (string): A description of the profile
* type (string): One of CREATED_BU_USER, or CREATED_BY_SYSTEM

#### Example response

    [
       {
          "name" : "Blocked by Anomaly Detector",
          "type" : "CREATED_BY_SYSTEM",
          "description" : "Internet access blocked by anomaly detector",
          "id" : "blocked_by_ad"
       },
       {
          "type" : "CREATED_BY_USER",
          "name" : "Deny all",
          "id" : "deny_all",
          "description" : "Deny all access to the Internet"
       },
       {
          "id" : "deny_all_except_sidn",
          "description" : "Deny all access to the Internet, but allow access to SIDN",
          "name" : "Deny all except SIDN",
          "type" : "CREATED_BY_USER"
       },
       {
          "description" : "Allow all IPv6 traffic, Deny all IPv4 traffic",
          "id" : "ipv6_only",
          "name" : "IPv6 only",
          "type" : "CREATED_BY_USER"
       }
    ]

### /spin_api/configuration

(note: this endpoint is temporary, or will at the very least change in the future)

**Method(s)**: GET, POST

**URL Parameters**: None

**Content Parameters \[POST only\]**:
  * config (JSON data structure): The data to store

This is an endpoint where applications can store arbitrary data for persistency.

For GET, returns the JSON data that was stored in the most recent POST call to this endpoint.

For POST, stores the data in the content parameter 'config'. This can be any arbitrary JSON data structure, and it will be returned on the next GET request to this endpoint.


## Other URLs

### /spin_api/tcpdump

**URL Parameter**: device (mac address)

Management page of the pcap capture format tool.

This page allows the user to download a live traffic dump of a device directly from the SPIN system.

The management page is not a RESTful API; it returns HTML pages.

The 'device' parameter is of the form 'aa:bb:cc:dd:ee:ff'.


### /spin_api/tcpdump_status

**URL Parameter**: device (mac address)

Page chunk showing the status (total bytes sent) of the pcap capture progress.

### /spin_api/tcpdump_start

**URL Parameter**: device (mac address)

Starts the tcpdump managed by the /spin_api/tcpdump page

### /spin_api/tcpdump_stop

**URL Parameter**: device (mac address)

Stops the tcpdump managed by the /spin_api/tcpdump page

