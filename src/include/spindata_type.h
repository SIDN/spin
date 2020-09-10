#ifndef SPIN_DATA_TYPE_H
#define SPIN_DATA_TYPE_H 1

#include "cJSON.h"

// basic data type for serializing spin data
// This is just a low-level wrapper for cJSON data structures

typedef cJSON *spin_data;

char *spin_data_serialize(spin_data sd);
void spin_data_ser_delete(char *str);
void spin_data_delete(spin_data sd);

#endif // SPIN_DATA_TYPE_H