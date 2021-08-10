# Specification for poll loop interface

Various subsystems can register
Info needed:

- File descriptor or 0 if none
- Timeout in millisec or none
- Routine to call for action (paramter for timeout or not)

## Poll loop
For each subsystem calculate latest time to act
Call poll with all fd's and timeout as just calculated
