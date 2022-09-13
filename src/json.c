#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

static uint32_t get_block_size_in_symbols(const char **p_symbols,
                                          size_t p_starting_symbol,
                                          size_t p_symbol_count)
{
    const char opening_char = p_symbols[p_starting_symbol][0];
    char closing_char;
    if (opening_char == '{')
    {
        closing_char = '}';
    }
    else if (opening_char == '[')
    {
        closing_char = ']';
    }
    else
    {
        fprintf(stderr, "[ERROR]: Unknown block-opening character '%c'\n",
                opening_char);
        return 0;
    }

    uint32_t required_closing_chars_to_close = 1;

    uint32_t result = 1;

    for (uint32_t i = (uint32_t)p_starting_symbol + 1; i < p_symbol_count; i++)
    {
        if (p_symbols[i][0] == opening_char)
        {
            required_closing_chars_to_close += 1;
        }

        if (p_symbols[i][0] == closing_char)
        {
            required_closing_chars_to_close -= 1;
        }

        if (required_closing_chars_to_close == 0)
        {
            break;
        }

        result += 1;
    }

    return result;
}

static uint32_t get_object_field_count(const char **p_symbols,
                                       uint32_t p_symbol_count)
{
    uint32_t result = 0;

    uint32_t required_closing_chars = 0;
    char closing_char = 0;

    for (uint32_t i = 0; i < p_symbol_count; i += 1)
    {
        const char *symbol = p_symbols[i];

        if (required_closing_chars == 0 && strcmp(symbol, ":") == 0)
        {
            result += 1;
        }
        else if (strcmp(symbol, "{") == 0 &&
                 (closing_char == '}' || closing_char == 0))
        {
            required_closing_chars += 1;
            closing_char = '}';
        }
        else if (strcmp(symbol, "[") == 0 &&
                 (closing_char == ']' || closing_char == 0))
        {
            required_closing_chars += 1;
            closing_char = ']';
        }
        else if (symbol[0] == closing_char && required_closing_chars != 0)
        {
            required_closing_chars -= 1;
        }

        if (required_closing_chars == 0)
        {
            closing_char = 0;
        }
    }

    return result;
}

struct json_object json_parse_object(const char **p_tokens,
                                     uint32_t p_token_count, uint8_t *p_status)
{
    struct json_object result;
    result.number_of_fields = get_object_field_count(
        p_tokens + 1,
        p_token_count - 1); // Exclude the beginning and ending braces
    result.fields = malloc(sizeof(struct json_field) * result.number_of_fields);

    uint32_t symbol_index = 0;
    uint32_t required_closing_chars = 0;
    char closing_char = 0;

    if (strcmp(p_tokens[0], "{") != 0)
    {
        fprintf(stderr, "[ERROR]: Object does not seem to start with '{'\n");
        *p_status = 0;
        return result;
    }

    for (uint32_t i = 0; i < result.number_of_fields; i++)
    {
        // Closing braces indicates the end of the object.
        if ((required_closing_chars == 0 || closing_char != '}') &&
            strcmp(p_tokens[symbol_index], "}") == 0)
        {
            break;
        }

        // Iterate until hitting a string symbol
        while (p_tokens[symbol_index][0] != '\"' &&
               p_tokens[symbol_index][strlen(p_tokens[symbol_index]) - 1] !=
                   '\"')
        {
            symbol_index += 1;
        }

        // Extract the name of the field
        const size_t name_size = strlen(p_tokens[symbol_index]) - 2;
        char *field_name = malloc((name_size + 1) * sizeof(char));
        strncpy(field_name, p_tokens[symbol_index] + 1, name_size);

        // Ensures that it will be null terminated
        field_name[name_size] = 0;

        symbol_index += 1;
        if (p_tokens[symbol_index][0] != ':')
        {
            fprintf(stderr, "[ERROR]: Expected ':' after field name of '%s'\n",
                    field_name);
            *p_status = 0;
            free(field_name);
            return result;
        }

        symbol_index += 1;

        union json_field_value field_value;
        enum json_field_type field_type;
        if (p_tokens[symbol_index][0] == '\"' &&
            p_tokens[symbol_index][strlen(p_tokens[symbol_index]) - 1] == '\"')
        {
            field_type = JSON_FIELD_TYPE_STRING;
            const size_t field_size = strlen(p_tokens[symbol_index]) - 2;
            char *field_string_value = malloc((field_size + 1) * sizeof(char));
            strncpy(field_string_value, p_tokens[symbol_index] + 1, field_size);

            // Ensure null termination
            field_string_value[field_size] = 0;

            field_value.string_value = field_string_value;
        }
        else if (strcmp(p_tokens[symbol_index], "true") == 0)
        {
            field_type = JSON_FIELD_TYPE_BOOLEAN;
            field_value.boolean_value = 1;
        }
        else if (strcmp(p_tokens[symbol_index], "false") == 0)
        {
            field_type = JSON_FIELD_TYPE_BOOLEAN;
            field_value.boolean_value = 0;
        }
        else if (p_tokens[symbol_index][0] == '{')
        {
            const uint32_t block_size = get_block_size_in_symbols(
                p_tokens, symbol_index, p_token_count);
            uint8_t status = 0;

            field_type = JSON_FIELD_TYPE_OBJECT;
            field_value.object_value =
                json_parse_object(p_tokens + symbol_index, block_size, &status);

            if (!status)
            {
                fprintf(
                    stderr,
                    "[ERROR]: Too many errors processing child object '%s'.",
                    field_name);
            }

            symbol_index += block_size;
        }
        else if (p_tokens[symbol_index][0] == '[')
        {
            const uint32_t block_size = get_block_size_in_symbols(
                p_tokens, symbol_index, p_token_count);
            uint8_t status = 0;

            field_type = JSON_FIELD_TYPE_ARRAY;
            field_value.array_value =
                json_parse_array(p_tokens + symbol_index, block_size, &status);

            if (!status)
            {
                fprintf(stderr,
                        "[ERROR]: Too many errors processing child array '%s'.\n",
                        field_name);
            }

            symbol_index += block_size;
        }
        else
        {
            fprintf(stderr, "[ERROR]: Expected value at field '%s'\n",
                    field_name);
            *p_status = 0;
            free(field_name);
            return result;
        }

        // TODO: handle numbers.

        symbol_index += 1;

        if (p_tokens[symbol_index][0] != ',' &&
            p_tokens[symbol_index][0] != '}')
        {
            fprintf(stderr, "[ERROR]: Expected ',' after field '%s'\n",
                    field_name);
            *p_status = 0;

            free(field_name);

            if (field_type == JSON_FIELD_TYPE_STRING)
            {
                free((void *)field_value.string_value);
            }

            return result;
        }

        struct json_field field;
        field.name = field_name;
        field.type = field_type;
        field.value = field_value;

        result.fields[i] = field;
    }

    *p_status = 1;
    return result;
}

struct json_array json_parse_array(const char **p_tokens,
                                   uint32_t p_token_count, uint8_t *p_status)
{
    struct json_array result;
    result.elements = NULL;
    result.number_of_elements = 0;
    
    *p_status = 1;

    return result;
}

void json_free_object(struct json_object *p_object)
{
    for (uint32_t i = 0; i < p_object->number_of_fields; i++)
    {
        free((void*)(p_object->fields[i].name));
        p_object->fields[i].name = NULL;
        
        if (p_object->fields[i].type == JSON_FIELD_TYPE_STRING)
        {
            free((void*)(p_object->fields[i].value.string_value));
        }
        else if (p_object->fields[i].type == JSON_FIELD_TYPE_OBJECT)
        {
            json_free_object(&(p_object->fields[i].value.object_value));
        }
    }
}
