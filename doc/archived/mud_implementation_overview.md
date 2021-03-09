
MUD manager solution requirements.

Some requirements to choose or build a mud manager.

Potential Requirements:
1. available or compilable for OpenWRT
2. available or compilable for other operating systems
3. support for OpenWRT Firewall implementation
4. support for other firewall implementations
5. lateral traffic support not necessary (wouldn't be able to do it from SPIN either)
6. support for latest draft(s)
7. Completeness of support for MUD file format
8. support for DHCP servers
    * dnsmasq
    * odhcpd
9. licence
10. amount of work (relative)
11. support for extensions like merging or superceding/overriding profiles?
12. (internal abstract representation vs direct JSON parsing)
13. integreerbaarheid(even kijken wat we daar precies onder verstaan, deels license en upstreamen van changes, maar ook changes in het algemeen kunnen maken?)
14. hoe actief is het project
15. CPE-support (tegenhanger van de lateral/switch support)

Potential solutions:
1. OSMud [https://osmud.org](https://osmud.org)
2. CIRA MUD Manager [https://github.com/CIRALabs/shg-mud-controller](https://github.com/CIRALabs/shg-mud-controller)
3. Cisco DevNet MUD Manager [https://github.com/CiscoDevNet/MUD-Manager](https://github.com/CiscoDevNet/MUD-Manager)
4. NIST-MUD [https://github.com/usnistgov/nist-mud](https://github.com/usnistgov/nist-mud)
5. Figshare [https://figshare.com/articles/Manufacturer_Usage_Description_Specification_Implementation/5552923](https://figshare.com/articles/Manufacturer_Usage_Description_Specification_Implementation/5552923)
7. Self-developed version

(MasterPeace also worked on mud, but apparently contributed to osmud)

| Name | Openwrt support | other OS | OpenWRT FW | Other FW | Lateral traffic | Latest draft/RFC? | Complete format support |
|---|---|---|---|---|---|---|---|
|osMUD | yes | linux, builds on ubuntu | yes | no (probably any iptables) | no |  documentation mentions draft, not rfc | Unspecified | | | | | | |
|CIRA SHG-MUD | yes | unknown | yes | unknown  |no | unspecified | unspecified |
| Cisco DevNet MUD Manager | no (might compile but nothing in docs) | ubuntu (probably all debian) | unspecified | cisco router? | no | unspecified | unspecified |
| NIST-MUD | no | openflow-devices | no | openflow-devices | unknown (probably) | no | yes |
| Figshare | no | no | no | no | no | no | no |
|self-developed|yes|yes|yes|yes|no|yes|partial|


| Name | DHCP support | Licence | Est. amount of work needed | Support for extensions | Internal abstraction vs direct json parsing | Integratability (fwiw) | Project activity | CPE support |
|---|---|---|---|---|---|---|---|---|
|osmud | [indirect](https://github.com/osmud/osmud/tree/master/src/dnsmasq) | Apache 2.0 | Reasonable (may need to contribute) | Yes (not built-in but source available) | direct parsing | active | decent | yes
|cira | indirect | Apache 2.0 | Reasonable (may need to contribute) | Likely | Direct parsing | unspecified | active | yes
|cisco | yes | BSD 3-clause | N/A | Unlikely | Direct parsing | hard (seems cisco-specific) | active | yes (assuming cisco) |
|nist-mud| no | NIST public | N/A | Unlikely | YANG-derived models | hard (openflow-based) | active| no (targets switches) |
|figshare| unknown | unknown | unknown| unknown| unknown | unknown | unknown | unknown |
|self-developed| yes | GPL or BSD | A lot | Yes | Abstraction | Full | not active | yes | 


