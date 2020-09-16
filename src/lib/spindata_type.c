#include <stdlib.h>

#include "spindata_type.h"

char *
spin_data_serialize(spin_data sd) {
    char *result;

    result = cJSON_PrintUnformatted(sd);

    // result is malloced, should be freed
    return result;
}

void
spin_data_ser_delete(char *str) {

    free(str);
}

void
spin_data_delete(spin_data sd) {

    cJSON_Delete(sd);
}

