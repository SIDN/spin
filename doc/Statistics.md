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
In code this would just look like

	static stat_t ctr = { stat_modname, "Interesting thing", STAT_TOTAL };
	
with stat_modname a string constant at the top of the file. This struct declaration could even be included inside a function code
	
calls while running

	stat_val(&ctr, val);
	
Statistics package will do something intelligent

	counter -> just add 1
	total -> just add value
	max -> increase max if
	
At regular intervals the statistics package will report JSON data containing all values like:

	Results: array of ( package, array of (name, type, value, count))
	
but then properly JSONified. Simpler is multiple reports of the form:

	package, name, type, value, count

These reports will be published on the SPIN/statistics channel, and it is left to external commands to make sense of them.

