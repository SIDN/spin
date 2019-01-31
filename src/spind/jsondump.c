#ifdef notdef
static int json_dump(const char *js, jsmntok_t *t, size_t count, int indent) {
    int i, j, k;
    if (count == 0) {
        return 0;
    }
    if (t->type == JSMN_PRIMITIVE) {
        printf("%.*s", t->end - t->start, js+t->start);
        return 1;
    } else if (t->type == JSMN_STRING) {
        printf("'%.*s'", t->end - t->start, js+t->start);
        return 1;
    } else if (t->type == JSMN_OBJECT) {
        printf("\n");
        j = 0;
        for (k = 0; k < indent; k++) printf("  ");
        printf("{\n");
        for (i = 0; i < t->size; i++) {
            for (k = 0; k < indent; k++) printf("  ");
            j += json_dump(js, t+1+j, count-j, indent+1);
            printf(": ");
            j += json_dump(js, t+1+j, count-j, indent+1);
            printf(" \n");
        }
        for (k = 0; k < indent; k++) printf("  ");
        printf("}");
        return j+1;
    } else if (t->type == JSMN_ARRAY) {
        j = 0;
        printf("\n");
        for (i = 0; i < t->size; i++) {
            for (k = 0; k < indent-1; k++) printf("  ");
            printf("   - ");
            j += json_dump(js, t+1+j, count-j, indent+1);
            printf("\n");
        }
        return j+1;
    }
    return 0;
}
#endif

