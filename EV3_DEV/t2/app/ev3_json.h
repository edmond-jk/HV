#define JSON_COMMAND_STRING "json"
#define JSON_MAX_KEY_VALUE_LENGTH 128 // Not per JSON spec - just EV limit
#define JSON_MAX_ARRAY 128            // Not per JSON spec - just EV limit

//#define JSON_DEBUG 

// Object be be a single key/value pair _OR_
// An array of key/value pairs _OR_
// An array of objects of any type _OR_
// A primitive

typedef enum
{
    JSON_UNDEFINED,     // Entry is not defined
    JSON_NESTED,        // Entry has a nested object
    JSON_ARRAY,         // Entry contains array
    JSON_STRING,        // Entry is a simple key/value pair using strings
    JSON_PRIMITIVE,     // Entry is a simple key/value pair containing a primitive value
} json_category_t;

typedef enum
{
    JSON_ENTRY_APPEND,        // Append entry to end of list
    JSON_ENTRY_INDENT,        // Add a new sub level to the current entry
} json_action_t;


struct json_object;     // Forward declaration

typedef struct key_value_pair
{
    char key[JSON_MAX_KEY_VALUE_LENGTH];    
    char value[JSON_MAX_KEY_VALUE_LENGTH];    
} key_value_pair_t;

typedef union json_union
{
    char value[JSON_MAX_KEY_VALUE_LENGTH];    
    struct json_object *json_object;
} json_union_t;

typedef struct key_object_pair
{
    char key[JSON_MAX_KEY_VALUE_LENGTH];    
    struct json_object *json_object;
} key_object_pair_t;

typedef union json_value_union
{
    json_category_t category;
    struct key_value_pair key_value;
    struct key_object_pair key_object;
} json_value_union_t;

typedef struct json_object
{
    struct json_object *previous;
    struct json_object *next;
    char key[JSON_MAX_KEY_VALUE_LENGTH];    
    json_category_t category;
    union json_union json_entry;
} json_object_t;


struct json_object *ev_json_create_object(char *key_string);
void ev_json_destroy_object(struct json_object *jo);

struct json_object *ev_json_add_key_pair_entry (struct json_object *jo, char *key, char *value, json_action_t action); 
struct json_object *ev_json_add_key_nested_entry (struct json_object *jo, char *key, json_action_t action); 
void ev_json_show_object(struct json_object *jo);
struct json_object *ev_json_add_object_nested_entry (struct json_object *jo, char *key, struct json_object *jo2, json_action_t action);
