# Statistics

Gather and distribute statistics in a structured and efficient way

Per counter a struct:

	Name of module
	Name of variable
	Type (total, max, ...?)
	Value (just an int)
	Count (just an int)
	Flags and pointers or whatever
	
	
The fields value and count together can be used for averages in UI.
In code this just looks like

	#include "statistics.h"
	STAT_MODULE(module-name)
	
at the top of the module, and for each counter a definition(inside or outside a function)

	STAT_COUNTER(ctr1, counter1-name, STAT_TOTAL);
	STAT_COUNTER(ctr2, counter2-name, STAT_MAX);

calls while running

	STAT_VALUE(ctr1, value);
	
Statistics package keeps track of number of times counter was accessed, and either the TOTAL or MAX of the values.

All names, module and counter, can be typed without quotes. The statistics.h file takes care of all that.

	
At regular intervals the statistics package reports JSON data containing all values with multiple reports of the form:

	package, name, type, value, count

These reports are published on the SPIN/stat channel, and it is left to external programs to make sense of them.

All counters and all code will disappear from compiled code at the unsetting of one preprocessor variable.