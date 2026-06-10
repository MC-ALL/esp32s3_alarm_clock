#include <third_party/cJSON.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	char *data;
	size_t len;
	size_t cap;
} json_print_buf_t;

static cJSON *json_new_item(unsigned char type)
{
	cJSON *item = (cJSON *)calloc(1, sizeof(*item));
	if (item != NULL) {
		item->type = type;
	}
	return item;
}

static char *json_strdup_len(const char *start, size_t len)
{
	char *out = (char *)malloc(len + 1U);
	if (out == NULL) {
		return NULL;
	}
	memcpy(out, start, len);
	out[len] = '\0';
	return out;
}

static void json_skip_ws(const char **p)
{
	while (p != NULL && *p != NULL && **p != '\0' && isspace((unsigned char)**p)) {
		(*p)++;
	}
}

static bool json_match_literal(const char **p, const char *literal)
{
	const size_t len = strlen(literal);
	if (strncmp(*p, literal, len) != 0) {
		return false;
	}
	*p += len;
	return true;
}

static char *json_parse_string_value(const char **p)
{
	if (p == NULL || *p == NULL || **p != '"') {
		return NULL;
	}
	(*p)++;

	size_t cap = 16U;
	size_t len = 0;
	char *out = (char *)malloc(cap);
	if (out == NULL) {
		return NULL;
	}

	while (**p != '\0' && **p != '"') {
		char ch = **p;
		if (ch == '\\') {
			(*p)++;
			ch = **p;
			if (ch == '\0') {
				free(out);
				return NULL;
			}
			switch (ch) {
			case '"':
			case '\\':
			case '/':
				break;
			case 'b':
				ch = '\b';
				break;
			case 'f':
				ch = '\f';
				break;
			case 'n':
				ch = '\n';
				break;
			case 'r':
				ch = '\r';
				break;
			case 't':
				ch = '\t';
				break;
			default:
				break;
			}
		}
		if ((len + 1U) >= cap) {
			cap *= 2U;
			char *next = (char *)realloc(out, cap);
			if (next == NULL) {
				free(out);
				return NULL;
			}
			out = next;
		}
		out[len++] = ch;
		(*p)++;
	}

	if (**p != '"') {
		free(out);
		return NULL;
	}
	(*p)++;
	out[len] = '\0';
	return out;
}

static void json_add_child(cJSON *parent, cJSON *child)
{
	if (parent == NULL || child == NULL) {
		return;
	}
	if (parent->child == NULL) {
		parent->child = child;
		return;
	}
	cJSON *tail = parent->child;
	while (tail->next != NULL) {
		tail = tail->next;
	}
	tail->next = child;
}

static cJSON *json_parse_value(const char **p);

static cJSON *json_parse_array(const char **p)
{
	if (**p != '[') {
		return NULL;
	}
	(*p)++;
	cJSON *array = cJSON_CreateArray();
	if (array == NULL) {
		return NULL;
	}
	json_skip_ws(p);
	if (**p == ']') {
		(*p)++;
		return array;
	}
	for (;;) {
		json_skip_ws(p);
		cJSON *value = json_parse_value(p);
		if (value == NULL) {
			cJSON_Delete(array);
			return NULL;
		}
		json_add_child(array, value);
		json_skip_ws(p);
		if (**p == ']') {
			(*p)++;
			return array;
		}
		if (**p != ',') {
			cJSON_Delete(array);
			return NULL;
		}
		(*p)++;
	}
}

static cJSON *json_parse_object(const char **p)
{
	if (**p != '{') {
		return NULL;
	}
	(*p)++;
	cJSON *object = cJSON_CreateObject();
	if (object == NULL) {
		return NULL;
	}
	json_skip_ws(p);
	if (**p == '}') {
		(*p)++;
		return object;
	}
	for (;;) {
		json_skip_ws(p);
		char *key = json_parse_string_value(p);
		if (key == NULL) {
			cJSON_Delete(object);
			return NULL;
		}
		json_skip_ws(p);
		if (**p != ':') {
			free(key);
			cJSON_Delete(object);
			return NULL;
		}
		(*p)++;
		json_skip_ws(p);
		cJSON *value = json_parse_value(p);
		if (value == NULL) {
			free(key);
			cJSON_Delete(object);
			return NULL;
		}
		value->string = key;
		json_add_child(object, value);
		json_skip_ws(p);
		if (**p == '}') {
			(*p)++;
			return object;
		}
		if (**p != ',') {
			cJSON_Delete(object);
			return NULL;
		}
		(*p)++;
	}
}

static cJSON *json_parse_number(const char **p)
{
	char *end = NULL;
	double value = strtod(*p, &end);
	if (end == *p) {
		return NULL;
	}
	*p = end;
	return cJSON_CreateNumber(value);
}

static cJSON *json_parse_value(const char **p)
{
	json_skip_ws(p);
	if (**p == '{') {
		return json_parse_object(p);
	}
	if (**p == '[') {
		return json_parse_array(p);
	}
	if (**p == '"') {
		char *value = json_parse_string_value(p);
		if (value == NULL) {
			return NULL;
		}
		cJSON *item = json_new_item(cJSON_String);
		if (item == NULL) {
			free(value);
			return NULL;
		}
		item->valuestring = value;
		return item;
	}
	if (json_match_literal(p, "true")) {
		return cJSON_CreateBool(true);
	}
	if (json_match_literal(p, "false")) {
		return cJSON_CreateBool(false);
	}
	if (json_match_literal(p, "null")) {
		return json_new_item(cJSON_NULL);
	}
	return json_parse_number(p);
}

cJSON *cJSON_Parse(const char *value)
{
	if (value == NULL) {
		return NULL;
	}
	const char *p = value;
	cJSON *root = json_parse_value(&p);
	if (root == NULL) {
		return NULL;
	}
	json_skip_ws(&p);
	if (*p != '\0') {
		cJSON_Delete(root);
		return NULL;
	}
	return root;
}

void cJSON_Delete(cJSON *item)
{
	while (item != NULL) {
		cJSON *next = item->next;
		cJSON_Delete(item->child);
		free(item->string);
		free(item->valuestring);
		free(item);
		item = next;
	}
}

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string)
{
	if (!cJSON_IsObject(object) || string == NULL) {
		return NULL;
	}
	for (cJSON *child = object->child; child != NULL; child = child->next) {
		if (child->string != NULL && strcmp(child->string, string) == 0) {
			return child;
		}
	}
	return NULL;
}

int cJSON_GetArraySize(const cJSON *array)
{
	if (!cJSON_IsArray(array)) {
		return 0;
	}
	int count = 0;
	for (cJSON *child = array->child; child != NULL; child = child->next) {
		count++;
	}
	return count;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index)
{
	if (!cJSON_IsArray(array) || index < 0) {
		return NULL;
	}
	int i = 0;
	for (cJSON *child = array->child; child != NULL; child = child->next, i++) {
		if (i == index) {
			return child;
		}
	}
	return NULL;
}

cJSON *cJSON_CreateObject(void)
{
	return json_new_item(cJSON_Object);
}

cJSON *cJSON_CreateArray(void)
{
	return json_new_item(cJSON_Array);
}

cJSON *cJSON_CreateString(const char *string)
{
	cJSON *item = json_new_item(cJSON_String);
	if (item == NULL) {
		return NULL;
	}
	item->valuestring = json_strdup_len(string != NULL ? string : "", strlen(string != NULL ? string : ""));
	if (item->valuestring == NULL) {
		cJSON_Delete(item);
		return NULL;
	}
	return item;
}

cJSON *cJSON_CreateNumber(double number)
{
	cJSON *item = json_new_item(cJSON_Number);
	if (item != NULL) {
		item->valuedouble = number;
		item->valueint = (int)number;
	}
	return item;
}

cJSON *cJSON_CreateBool(bool boolean)
{
	cJSON *item = json_new_item(boolean ? cJSON_True : cJSON_False);
	if (item != NULL) {
		item->valueint = boolean ? 1 : 0;
	}
	return item;
}

bool cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item)
{
	if (!cJSON_IsObject(object) || string == NULL || item == NULL) {
		cJSON_Delete(item);
		return false;
	}
	item->string = json_strdup_len(string, strlen(string));
	if (item->string == NULL) {
		cJSON_Delete(item);
		return false;
	}
	json_add_child(object, item);
	return true;
}

bool cJSON_AddItemToArray(cJSON *array, cJSON *item)
{
	if (!cJSON_IsArray(array) || item == NULL) {
		cJSON_Delete(item);
		return false;
	}
	json_add_child(array, item);
	return true;
}

cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *string)
{
	cJSON *item = cJSON_CreateString(string);
	return cJSON_AddItemToObject(object, name, item) ? item : NULL;
}

cJSON *cJSON_AddNumberToObject(cJSON *object, const char *name, double number)
{
	cJSON *item = cJSON_CreateNumber(number);
	return cJSON_AddItemToObject(object, name, item) ? item : NULL;
}

cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, bool boolean)
{
	cJSON *item = cJSON_CreateBool(boolean);
	return cJSON_AddItemToObject(object, name, item) ? item : NULL;
}

static bool json_buf_append(json_print_buf_t *buf, const char *text, size_t len)
{
	if (buf == NULL || text == NULL) {
		return false;
	}
	if ((buf->len + len + 1U) > buf->cap) {
		size_t next_cap = buf->cap == 0U ? 64U : buf->cap;
		while ((buf->len + len + 1U) > next_cap) {
			next_cap *= 2U;
		}
		char *next = (char *)realloc(buf->data, next_cap);
		if (next == NULL) {
			return false;
		}
		buf->data = next;
		buf->cap = next_cap;
	}
	memcpy(buf->data + buf->len, text, len);
	buf->len += len;
	buf->data[buf->len] = '\0';
	return true;
}

static bool json_buf_append_c(json_print_buf_t *buf, char ch)
{
	return json_buf_append(buf, &ch, 1U);
}

static bool json_print_string(json_print_buf_t *buf, const char *text)
{
	if (!json_buf_append_c(buf, '"')) {
		return false;
	}
	for (const char *p = text != NULL ? text : ""; *p != '\0'; p++) {
		switch (*p) {
		case '"':
			if (!json_buf_append(buf, "\\\"", 2U)) {
				return false;
			}
			break;
		case '\\':
			if (!json_buf_append(buf, "\\\\", 2U)) {
				return false;
			}
			break;
		case '\n':
			if (!json_buf_append(buf, "\\n", 2U)) {
				return false;
			}
			break;
		case '\r':
			if (!json_buf_append(buf, "\\r", 2U)) {
				return false;
			}
			break;
		case '\t':
			if (!json_buf_append(buf, "\\t", 2U)) {
				return false;
			}
			break;
		default:
			if (!json_buf_append_c(buf, *p)) {
				return false;
			}
			break;
		}
	}
	return json_buf_append_c(buf, '"');
}

static bool json_print_value(json_print_buf_t *buf, const cJSON *item)
{
	if (item == NULL) {
		return json_buf_append(buf, "null", 4U);
	}
	if (item->type == cJSON_False) {
		return json_buf_append(buf, "false", 5U);
	}
	if (item->type == cJSON_True) {
		return json_buf_append(buf, "true", 4U);
	}
	if (item->type == cJSON_NULL) {
		return json_buf_append(buf, "null", 4U);
	}
	if (item->type == cJSON_Number) {
		char text[32];
		int len = snprintf(text, sizeof(text), "%.15g", item->valuedouble);
		return len > 0 && json_buf_append(buf, text, (size_t)len);
	}
	if (item->type == cJSON_String) {
		return json_print_string(buf, item->valuestring);
	}
	if (item->type == cJSON_Array) {
		if (!json_buf_append_c(buf, '[')) {
			return false;
		}
		for (const cJSON *child = item->child; child != NULL; child = child->next) {
			if (child != item->child && !json_buf_append_c(buf, ',')) {
				return false;
			}
			if (!json_print_value(buf, child)) {
				return false;
			}
		}
		return json_buf_append_c(buf, ']');
	}
	if (item->type == cJSON_Object) {
		if (!json_buf_append_c(buf, '{')) {
			return false;
		}
		for (const cJSON *child = item->child; child != NULL; child = child->next) {
			if (child != item->child && !json_buf_append_c(buf, ',')) {
				return false;
			}
			if (!json_print_string(buf, child->string) || !json_buf_append_c(buf, ':') ||
			    !json_print_value(buf, child)) {
				return false;
			}
		}
		return json_buf_append_c(buf, '}');
	}
	return false;
}

char *cJSON_PrintUnformatted(const cJSON *item)
{
	json_print_buf_t buf = { 0 };
	if (!json_print_value(&buf, item)) {
		free(buf.data);
		return NULL;
	}
	return buf.data;
}
