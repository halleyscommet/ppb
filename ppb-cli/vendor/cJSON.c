/*
  cJSON.c - MIT License - https://github.com/DaveGamble/cJSON
  This is a lightly trimmed version sufficient for parsing small config files
  (objects, arrays, strings, numbers, booleans, null) and printing.
*/
#include "cJSON.h"
#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CJSON_NESTING_LIMIT
#define CJSON_NESTING_LIMIT 1000
#endif

static const char *ep = NULL;

static void *(*cJSON_malloc)(size_t sz) = malloc;
static void (*cJSON_free)(void *ptr) = free;

static char *cJSON_strdup(const char *src)
{
    if (!src) return NULL;
    size_t len = strlen(src);
    char *out = (char *)cJSON_malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, src, len + 1);
    return out;
}

static int cJSON_strcasecmp(const char *s1, const char *s2)
{
    if (!s1) return (s1 == s2) ? 0 : 1;
    if (!s2) return 1;
    for (; tolower((unsigned char)*s1) == tolower((unsigned char)*s2); ++s1, ++s2) {
        if (*s1 == '\0') return 0;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

void cJSON_InitHooks(cJSON_Hooks *hooks)
{
    if (!hooks) { /* reset */
        cJSON_malloc = malloc;
        cJSON_free = free;
        return;
    }
    cJSON_malloc = hooks->malloc_fn ? hooks->malloc_fn : malloc;
    cJSON_free = hooks->free_fn ? hooks->free_fn : free;
}

static cJSON *cJSON_New_Item(void)
{
    cJSON *node = (cJSON *)cJSON_malloc(sizeof(cJSON));
    if (node) {
        memset(node, 0, sizeof(cJSON));
    }
    return node;
}

static const char *skip(const char *in)
{
    while (in && *in && (unsigned char)*in <= 32) {
        in++;
    }
    return in;
}

static const char *parse_number(cJSON *item, const char *num)
{
    double n = 0;
    int sign = 1, scale = 0, subscale = 0, signsubscale = 1;

    if (*num == '-') sign = -1, num++;
    if (*num == '0') num++;
    if (*num >= '1' && *num <= '9') {
        do {
            n = (n * 10.0) + (*num - '0');
            num++;
        } while (*num >= '0' && *num <= '9');
    }
    if (*num == '.') {
        num++;
        while (*num >= '0' && *num <= '9') {
            n = (n * 10.0) + (*num - '0');
            scale--;
            num++;
        }
    }
    if (*num == 'e' || *num == 'E') {
        num++;
        if (*num == '+') num++;
        else if (*num == '-') signsubscale = -1, num++;
        while (*num >= '0' && *num <= '9') {
            subscale = (subscale * 10) + (*num - '0');
            num++;
        }
    }

    n = sign * n * pow(10.0, scale + subscale * signsubscale);

    item->valuedouble = n;
    item->valueint = (int)n;
    item->type = cJSON_Number;
    return num;
}

static const char *parse_hex4(const char *str, unsigned *out)
{
    *out = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)str[i];
        *out <<= 4;
        if (c >= '0' && c <= '9') *out |= (c - '0');
        else if (c >= 'A' && c <= 'F') *out |= (c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') *out |= (c - 'a' + 10);
        else return NULL;
    }
    return str + 4;
}

static const char *parse_string(cJSON *item, const char *str)
{
    const char *ptr = str + 1;
    char *ptr2;
    char *out;
    int len = 0;
    unsigned uc, uc2;

    if (*str != '"') { ep = str; return NULL; }

    while (*ptr != '"' && *ptr) {
        if (*ptr++ == '\\') ptr++; /* Skip escaped quotes. */
        len++;
    }
    if (!*ptr) { ep = str; return NULL; }

    out = (char *)cJSON_malloc(len + 1);
    if (!out) return NULL;

    ptr = str + 1;
    ptr2 = out;
    while (*ptr != '"' && *ptr) {
        if (*ptr != '\\') *ptr2++ = *ptr++;
        else {
            ptr++;
            switch (*ptr) {
            case 'b': *ptr2++ = '\b'; break;
            case 'f': *ptr2++ = '\f'; break;
            case 'n': *ptr2++ = '\n'; break;
            case 'r': *ptr2++ = '\r'; break;
            case 't': *ptr2++ = '\t'; break;
            case 'u':
                ptr = parse_hex4(ptr + 1, &uc);
                if (!ptr) { cJSON_free(out); return NULL; }
                if (uc >= 0xDC00 && uc <= 0xDFFF) { cJSON_free(out); return NULL; }
                if (uc >= 0xD800 && uc <= 0xDBFF) {
                    if (ptr[0] != '\\' || ptr[1] != 'u') { cJSON_free(out); return NULL; }
                    ptr = parse_hex4(ptr + 2, &uc2);
                    if (!ptr) { cJSON_free(out); return NULL; }
                    uc = 0x10000 + (((uc & 0x3FF) << 10) | (uc2 & 0x3FF));
                }
                len = 4;
                if (uc < 0x80) len = 1;
                else if (uc < 0x800) len = 2;
                else if (uc < 0x10000) len = 3;
                ptr2 += len;
                switch (len) {
                case 4: *--ptr2 = (char)((uc | 0x80) & 0xBF); uc >>= 6;
                case 3: *--ptr2 = (char)((uc | 0x80) & 0xBF); uc >>= 6;
                case 2: *--ptr2 = (char)((uc | 0x80) & 0xBF); uc >>= 6;
                case 1: *--ptr2 = (char)(uc | (len == 1 ? 0 : len == 2 ? 0xC0 : len == 3 ? 0xE0 : 0xF0));
                }
                ptr2 += len;
                break;
            default: *ptr2++ = *ptr; break;
            }
            ptr++;
        }
    }
    *ptr2 = '\0';
    if (*ptr == '"') ptr++;

    item->type = cJSON_String;
    item->valuestring = out;
    return ptr;
}

static const char *parse_value(cJSON *item, const char *value);
static const char *parse_array(cJSON *item, const char *value)
{
    if (*value != '[') { ep = value; return NULL; }
    item->type = cJSON_Array;
    value = skip(value + 1);
    if (*value == ']') return value + 1;

    item->child = cJSON_New_Item();
    if (!item->child) return NULL;
    value = skip(parse_value(item->child, skip(value)));
    if (!value) return NULL;

    cJSON *child = item->child;
    while (*value == ',') {
        cJSON *new_item = cJSON_New_Item();
        if (!new_item) return NULL;
        child->next = new_item; new_item->prev = child; child = new_item;
        value = skip(parse_value(child, skip(value + 1)));
        if (!value) return NULL;
    }

    if (*value == ']') return value + 1;
    ep = value; return NULL;
}

static const char *parse_object(cJSON *item, const char *value)
{
    if (*value != '{') { ep = value; return NULL; }
    item->type = cJSON_Object;
    value = skip(value + 1);
    if (*value == '}') return value + 1;

    item->child = cJSON_New_Item();
    if (!item->child) return NULL;

    value = skip(parse_string(item->child, skip(value)));
    if (!value) return NULL;
    item->child->string = item->child->valuestring; item->child->valuestring = NULL;

    if (*value != ':') { ep = value; return NULL; }
    value = skip(parse_value(item->child, skip(value + 1)));
    if (!value) return NULL;

    cJSON *child = item->child;
    while (*value == ',') {
        cJSON *new_item = cJSON_New_Item();
        if (!new_item) return NULL;
        child->next = new_item; new_item->prev = child; child = new_item;

        value = skip(parse_string(child, skip(value + 1)));
        if (!value) return NULL;
        child->string = child->valuestring; child->valuestring = NULL;
        if (*value != ':') { ep = value; return NULL; }
        value = skip(parse_value(child, skip(value + 1)));
        if (!value) return NULL;
    }

    if (*value == '}') return value + 1;
    ep = value; return NULL;
}

static const char *parse_value(cJSON *item, const char *value)
{
    if (!value) return NULL;
    if (!strncmp(value, "null", 4)) { item->type = cJSON_NULL; return value + 4; }
    if (!strncmp(value, "false", 5)) { item->type = cJSON_False; return value + 5; }
    if (!strncmp(value, "true", 4)) { item->type = cJSON_True; item->valueint = 1; return value + 4; }
    if (*value == '"') return parse_string(item, value);
    if (*value == '-' || (*value >= '0' && *value <= '9')) return parse_number(item, value);
    if (*value == '[') return parse_array(item, value);
    if (*value == '{') return parse_object(item, value);

    ep = value;
    return NULL;
}

cJSON *cJSON_ParseWithLength(const char *value, size_t buffer_length)
{
    if (value == NULL || buffer_length == 0) return NULL;
    cJSON *c = cJSON_New_Item();
    if (!c) return NULL;

    ep = NULL;
    const char *end = parse_value(c, skip(value));
    if (!end) { cJSON_Delete(c); return NULL; }

    return c;
}

cJSON *cJSON_Parse(const char *value)
{
    return cJSON_ParseWithLength(value, value ? strlen(value) : 0);
}

void cJSON_Delete(cJSON *c)
{
    cJSON *next;
    while (c) {
        next = c->next;
        if (!(c->type & cJSON_IsReference) && c->child) cJSON_Delete(c->child);
        if (!(c->type & cJSON_IsReference) && c->valuestring) cJSON_free(c->valuestring);
        if (c->string) cJSON_free(c->string);
        cJSON_free(c);
        c = next;
    }
}

int cJSON_GetArraySize(const cJSON *array)
{
    int size = 0;
    cJSON *c = array ? array->child : NULL;
    while (c) size++, c = c->next;
    return size;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int item)
{
    cJSON *c = array ? array->child : NULL;
    while (c && item > 0) item--, c = c->next;
    return c;
}

cJSON *cJSON_GetObjectItem(const cJSON *object, const char *string)
{
    cJSON *c = object ? object->child : NULL;
    while (c && cJSON_strcasecmp(c->string, string)) c = c->next;
    return c;
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string)
{
    cJSON *c = object ? object->child : NULL;
    while (c && strcmp(c->string, string)) c = c->next;
    return c;
}

int cJSON_IsString(const cJSON *item)
{
    return item && (item->type & 0xFF) == cJSON_String;
}

int cJSON_IsNumber(const cJSON *item)
{
    return item && (item->type & 0xFF) == cJSON_Number;
}

int cJSON_IsObject(const cJSON *item)
{
    return item && (item->type & 0xFF) == cJSON_Object;
}

int cJSON_IsArray(const cJSON *item)
{
    return item && (item->type & 0xFF) == cJSON_Array;
}

/* Printing functions (minimal) */
static char *print_string_ptr(const char *str)
{
    size_t len = 0;
    const char *ptr;
    char *out, *ptr2;

    if (!str) return NULL;

    ptr = str;
    while (*ptr) {
        if ((unsigned char)*ptr < 32 || *ptr == '"' || *ptr == '\\') len += 2;
        else len++;
        ptr++;
    }

    out = (char *)cJSON_malloc(len + 3);
    if (!out) return NULL;

    ptr2 = out;
    *ptr2++ = '"';
    ptr = str;
    while (*ptr) {
        if ((unsigned char)*ptr < 32 || *ptr == '"' || *ptr == '\\') {
            *ptr2++ = '\\';
            switch (*ptr) {
            case '\\': *ptr2++ = '\\'; break;
            case '"': *ptr2++ = '"'; break;
            case '\b': *ptr2++ = 'b'; break;
            case '\f': *ptr2++ = 'f'; break;
            case '\n': *ptr2++ = 'n'; break;
            case '\r': *ptr2++ = 'r'; break;
            case '\t': *ptr2++ = 't'; break;
            default:
                sprintf(ptr2, "u%04x", (unsigned char)*ptr);
                ptr2 += 5;
                break;
            }
        } else {
            *ptr2++ = *ptr;
        }
        ptr++;
    }
    *ptr2++ = '"';
    *ptr2++ = '\0';
    return out;
}

static char *print_value(const cJSON *item, int formatted, int depth);
static char *print_array(const cJSON *item, int formatted, int depth)
{
    char **entries = NULL;
    char *out = NULL, *ptr;
    int len = 5;
    int count = cJSON_GetArraySize(item);
    if (!count) {
        out = (char *)cJSON_malloc(3);
        if (out) strcpy(out, "[]");
        return out;
    }

    entries = (char **)cJSON_malloc(count * sizeof(char *));
    if (!entries) return NULL;

    cJSON *child = item->child;
    for (int i = 0; child && i < count; i++, child = child->next) {
        entries[i] = print_value(child, formatted, depth + 1);
        if (!entries[i]) { for (int j = 0; j < i; j++) cJSON_free(entries[j]); cJSON_free(entries); return NULL; }
        len += (int)strlen(entries[i]) + 2 + (formatted ? 1 : 0);
    }

    out = (char *)cJSON_malloc(len + 1);
    if (!out) { for (int i = 0; i < count; i++) cJSON_free(entries[i]); cJSON_free(entries); return NULL; }

    *out = '['; ptr = out + 1;
    for (int i = 0; i < count; i++) {
        int l = (int)strlen(entries[i]);
        memcpy(ptr, entries[i], l); ptr += l;
        if (i != count - 1) { *ptr++ = ','; if (formatted) *ptr++ = ' '; }
        cJSON_free(entries[i]);
    }
    *ptr++ = ']'; *ptr++ = '\0';
    cJSON_free(entries);
    return out;
}

static char *print_object(const cJSON *item, int formatted, int depth)
{
    char **entries = NULL;
    char *out = NULL, *ptr;
    int len = 7;
    int count = 0;
    cJSON *child = item->child;
    while (child) count++, child = child->next;
    if (!count) {
        out = (char *)cJSON_malloc(3);
        if (out) strcpy(out, "{}");
        return out;
    }

    entries = (char **)cJSON_malloc(count * sizeof(char *));
    if (!entries) return NULL;

    child = item->child;
    for (int i = 0; child && i < count; i++, child = child->next) {
        char *key = print_string_ptr(child->string);
        char *val = print_value(child, formatted, depth + 1);
        int needed = (int)strlen(key) + (int)strlen(val) + 2;
        entries[i] = (char *)cJSON_malloc(needed + 1 + (formatted ? depth + 1 : 0));
        if (!entries[i]) { cJSON_free(key); cJSON_free(val); for (int j = 0; j < i; j++) cJSON_free(entries[j]); cJSON_free(entries); return NULL; }
        if (formatted) {
            memset(entries[i], '\t', depth + 1);
            sprintf(entries[i] + depth + 1, "%s:%s%s", key, formatted ? " " : "", val);
        } else {
            sprintf(entries[i], "%s:%s", key, val);
        }
        cJSON_free(key); cJSON_free(val);
        len += (int)strlen(entries[i]) + 2 + (formatted ? 1 : 0);
    }

    out = (char *)cJSON_malloc(len + 1 + (formatted ? depth : 0));
    if (!out) { for (int i = 0; i < count; i++) cJSON_free(entries[i]); cJSON_free(entries); return NULL; }

    ptr = out; *ptr++ = '{'; if (formatted) *ptr++ = '\n';
    for (int i = 0; i < count; i++) {
        int l = (int)strlen(entries[i]);
        memcpy(ptr, entries[i], l); ptr += l;
        if (i != count - 1) { *ptr++ = ','; if (formatted) *ptr++ = '\n'; }
        cJSON_free(entries[i]);
    }
    if (formatted) { *ptr++ = '\n'; memset(ptr, '\t', depth); ptr += depth; }
    *ptr++ = '}'; *ptr++ = '\0';
    cJSON_free(entries);
    return out;
}

static char *print_value(const cJSON *item, int formatted, int depth)
{
    char *out = NULL;
    switch (item->type & 0xFF) {
    case cJSON_NULL: out = cJSON_strdup("null"); break;
    case cJSON_False: out = cJSON_strdup("false"); break;
    case cJSON_True: out = cJSON_strdup("true"); break;
    case cJSON_Number:
        out = (char *)cJSON_malloc(32);
        if (out) {
            if (fabs(((double)item->valueint) - item->valuedouble) <= DBL_EPSILON && fabs(item->valuedouble) < 1.0e60)
                sprintf(out, "%d", item->valueint);
            else
                sprintf(out, "%f", item->valuedouble);
        }
        break;
    case cJSON_String: out = print_string_ptr(item->valuestring); break;
    case cJSON_Array: out = print_array(item, formatted, depth); break;
    case cJSON_Object: out = print_object(item, formatted, depth); break;
    default: break;
    }
    return out;
}

char *cJSON_Print(const cJSON *item)
{
    return print_value(item, 1, 0);
}

char *cJSON_PrintUnformatted(const cJSON *item)
{
    return print_value(item, 0, 0);
}

char *cJSON_PrintBuffered(const cJSON *item, int prebuffer, int fmt)
{
    (void)prebuffer; (void)fmt;
    return cJSON_Print(item);
}

int cJSON_PrintPreallocated(cJSON *item, char *buffer, const int length, const int format)
{
    char *out = format ? cJSON_Print(item) : cJSON_PrintUnformatted(item);
    if (!out) return 0;
    int out_len = (int)strlen(out);
    if (out_len + 1 > length) { cJSON_free(out); return 0; }
    memcpy(buffer, out, out_len + 1);
    cJSON_free(out);
    return 1;
}

/* Object/Array creation helpers (minimal) */
cJSON *cJSON_CreateNull(void) { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_NULL; return item; }
cJSON *cJSON_CreateTrue(void) { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_True; return item; }
cJSON *cJSON_CreateFalse(void) { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_False; return item; }
cJSON *cJSON_CreateBool(int b) { cJSON *item = cJSON_New_Item(); if (item) item->type = b ? cJSON_True : cJSON_False; return item; }
cJSON *cJSON_CreateNumber(double num) { cJSON *item = cJSON_New_Item(); if (item) { item->type = cJSON_Number; item->valuedouble = num; item->valueint = (int)num; } return item; }
cJSON *cJSON_CreateString(const char *string) { cJSON *item = cJSON_New_Item(); if (item) { item->type = cJSON_String; item->valuestring = cJSON_strdup(string); } return item; }
cJSON *cJSON_CreateArray(void) { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_Array; return item; }
cJSON *cJSON_CreateObject(void) { cJSON *item = cJSON_New_Item(); if (item) item->type = cJSON_Object; return item; }

void cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
    cJSON *c = array->child;
    if (!c) array->child = item;
    else { while (c && c->next) c = c->next; c->next = item; item->prev = c; }
}

void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item)
{
    item->string = cJSON_strdup(string);
    cJSON_AddItemToArray(object, item);
}

void cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item)
{
    item->string = (char *)string;
    item->type |= cJSON_StringIsConst;
    cJSON_AddItemToArray(object, item);
}

cJSON *cJSON_DetachItemFromArray(cJSON *array, int which)
{
    cJSON *c = array ? array->child : NULL;
    while (c && which > 0) c = c->next, which--;
    if (!c) return NULL;
    if (c->prev) c->prev->next = c->next;
    if (c->next) c->next->prev = c->prev;
    if (c == array->child) array->child = c->next;
    c->prev = c->next = NULL;
    return c;
}

void cJSON_DeleteItemFromArray(cJSON *array, int which)
{
    cJSON *c = cJSON_DetachItemFromArray(array, which);
    if (c) cJSON_Delete(c);
}
