#ifndef JSON_H
#define JSON_H

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUM,
    JSON_STR,
    JSON_ARR,
    JSON_OBJ
} json_type;

typedef struct json_value json_value;

struct json_value {
    json_type type;
    union {
        int boolean;
        double number;
        char *string;
        struct {
            json_value **items;
            int count;
        } array;
        struct {
            char **keys;
            json_value **values;
            int count;
        } object;
    } u;
};

json_value *json_parse(const char *text);
void        json_free(json_value *v);

json_value *json_get(json_value *v, const char *path);
json_value *json_at(json_value *v, int index);

int         json_count(json_value *v);
const char *json_str(json_value *v, const char *fallback);
double      json_num(json_value *v, double fallback);
int         json_int(json_value *v, int fallback);
int         json_bool(json_value *v, int fallback);

#endif
