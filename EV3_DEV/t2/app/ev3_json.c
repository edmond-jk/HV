#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ev3_json.h"

struct json_object *ev_json_create_object(char *key_string)
{
    struct json_object *jo; 

    jo = (struct json_object *)malloc(sizeof(json_object_t));
    jo->previous = NULL;
    jo->next = NULL;
    sprintf (jo->key,"%s", key_string);
    jo->category = JSON_UNDEFINED;

    return jo;
}

// TBD - go recursive
void ev_json_destroy_object(struct json_object *jo)
{
    free(jo);
}

struct json_object *ev_json_add_key_pair_entry (struct json_object *jo, char *key, char *value, json_action_t action)
{
    struct json_object *current_jo = jo;
    struct json_object *new_jo = NULL;
        
    // If the first entry does not have a key, make the first entry the key/value pair.
    if (strcmp("", current_jo->key) != 0)
    {
        new_jo = ev_json_create_object("");
        // Find end of list
        while (current_jo->next != NULL)
        {
            current_jo = current_jo->next;
        } 

        // Allocate an object and add as next or sub level as neeeded
        switch(action)
        {
            case JSON_ENTRY_APPEND:
#ifdef JSON_DEBUG
                printf("ev_json_add_key_pair_entry: current_jo=%p next=%p\n", current_jo,new_jo);
#endif
                current_jo->next = new_jo;
                current_jo = current_jo->next;
                break;
            case JSON_ENTRY_INDENT:
#ifdef JSON_DEBUG
                printf("ev_json_add_key_pair_entry: current_jo=%p json_entry.json_object=%p\n", current_jo,new_jo);
#endif
                current_jo->category = JSON_NESTED;
                current_jo->json_entry.json_object = new_jo;
                current_jo = current_jo->json_entry.json_object;
                break;
            default:
                break;
        }

        // We are passing the nested object, allways append to end of the list
        ev_json_add_key_pair_entry (current_jo, key, value, JSON_ENTRY_APPEND);
    }
    else
    {
        // This is the first entry and the object was created without a key.
        current_jo->category = JSON_STRING;
        sprintf(current_jo->key,"%s",key);
        sprintf(current_jo->json_entry.value,"%s",value);
#ifdef JSON_DEBUG
        printf("ev_json_add_key_pair_entry: current_jo=%p next=%p category=%d\n", current_jo,current_jo->next, current_jo->category);
#endif

    }

    // Make sure current_jo is always set to the entry that was added or modified.
    return current_jo;
}

struct json_object *ev_json_add_key_nested_entry (struct json_object *jo, char *key, json_action_t action)
{
    struct json_object *current_jo = jo;
    struct json_object *new_jo;

    new_jo = ev_json_create_object("");

    // If the first entry does not have a key, make the first entry the key/value pair.
    if (strcmp("", current_jo->key) != 0)
    {
        // Find end of list
        while (current_jo->next != NULL)
        {
            current_jo = current_jo->next;
        } 
        // Allocate an object and add as next
        current_jo->next = new_jo;
        current_jo = current_jo->next;
        current_jo->category = JSON_NESTED;
        sprintf(current_jo->key,"%s",key);
    }
    else
    {
        // This is the first entry and the object was created without a key.
        current_jo->category = JSON_NESTED;
        sprintf(current_jo->key,"%s",key);
        current_jo->next = new_jo;
    }

    return new_jo;
}

struct json_object *ev_json_add_object_nested_entry (struct json_object *jo, char *key, struct json_object *jo2, json_action_t action)
{
    struct json_object *current_jo = jo;
    struct json_object *new_jo;

    new_jo = jo2;

    // If first entry is used, find last entry and append
    if (current_jo->category != JSON_UNDEFINED)
    {
        new_jo = ev_json_create_object("");
        // Find end of list
        while (current_jo->next != NULL)
        {
            current_jo = current_jo->next;
        } 
        // Allocate an object and add as next
        current_jo->next = new_jo;
        current_jo = current_jo->next;
        current_jo->category = JSON_NESTED;
        sprintf(current_jo->key,"%s",key);
        current_jo->json_entry.json_object = jo2;
    }
    else
    {
        // This is the first entry and the object was created without a key.
        current_jo->category = JSON_NESTED;
        sprintf(current_jo->key,"%s",key);
        current_jo->json_entry.json_object = jo2;
    }

    return new_jo;
}

static void ev_json_indent(int nesting_level, int is_newline)
{
    int i;

    if (is_newline)
    {
        printf("\n");
    }

    for (i=0;i<nesting_level;i++)
    {
        printf("    ");  // Four spaces for each level
    }
}


// Recusive
void ev_json_show_object_aux(struct json_object *jo, int nesting_level)
{
    struct json_object *current_jo = jo;
    int loop_max = 200; // DBG - TBD - take out

    ev_json_indent(nesting_level, 1);

    if (jo != NULL)
    {
        if (jo->category == JSON_ARRAY)
        {
            printf("[");
        }
        else
        {
            printf("{");
        }
    }
    else
    {
        printf("{");
    }

    if (current_jo != NULL)
    {
        //if (strcmp("", current_jo->key) != 0)
        //if ((strcmp("", current_jo->key) != 0) || (current_jo->category != JSON_UNDEFINED))
        if (current_jo->category != JSON_UNDEFINED)
        {
            // Go through the array elements and display those.
            while ((current_jo != NULL) && (loop_max>0))
            {
#ifdef JSON_DEBUG
                printf("\nev_json_show_object_aux: current_jo=%p next=%p json_object=%p\n", current_jo, current_jo->next, current_jo->json_entry.json_object);
#endif


                if (strcmp("", current_jo->key) != 0)
                {
                    ev_json_indent(nesting_level, 1);
                    printf("\"%s\":", current_jo->key);   // show key portion if there is a key
                }
                else
                {
                    ev_json_indent(nesting_level, 0);
                }

                // Value portion
                switch (current_jo->category)
                {
                    case JSON_NESTED:
                        // Show sub object if any
                        ev_json_show_object_aux(current_jo->json_entry.json_object, nesting_level+1);
                        if (current_jo->next != NULL)
                        {
                            printf(",");
                        }
                        break;
                    case JSON_ARRAY:
                        break;
                    case JSON_STRING:
                        printf("\"%s\"", current_jo->json_entry.value);
                        if (current_jo->next != NULL)
                        {
                            // Add the comma if needed.
                            printf(",");
                        }
                        break;
                    case JSON_PRIMITIVE:
                        break;
                    default:
                        printf("ERROR: undefined = %d\n", current_jo->category);
                        break;
                }

                current_jo = current_jo->next;
                loop_max--;
            }
        }
    }

    ev_json_indent(nesting_level, 1);

    if (jo != NULL)
    {
        if (jo->category == JSON_ARRAY)
        {
            printf("]");
        }
        else
        {
            printf("}");
        }
    }
    else
    {
        printf("}");
    }
}

void ev_json_show_object(struct json_object *jo)
{
    int i;
    int nesting_level = 0;

    ev_json_show_object_aux(jo, nesting_level);
    printf("\n");
}


