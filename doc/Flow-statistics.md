# Flow statistics in spind

Keeping track of a moderate amount of flow statistics in spind is possible, but it requires new code and data-structures.  

Issues:
- Keeping track of all nodepairs for a certain amount of time or flows will possibly flush flows of quiter nodes by talkative nodes.
- Keeping track of a certain amount of flows per node will not have this problem, but the nodes they talked to might be gone. This could be solved by a certain type of persistence. If a certain node is used in other information do not do timeout on it.
- Keeping track of nodenumbers in general needs care when merging nodes.
