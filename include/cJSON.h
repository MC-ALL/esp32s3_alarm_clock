#ifndef APP_CJSON_H_
#define APP_CJSON_H_

#include <stdbool.h>
#include <stddef.h>

typedef struct cJSON {
	struct cJSON *next;
	struct cJSON *child;
	char *string;
	char *valuestring;
	double valuedouble;
	int valueint;
	unsigned char type;
} cJSON;

#define cJSON_False 1U
#define cJSON_True 2U
#define cJSON_NULL 4U
#define cJSON_Number 8U
#define cJSON_String 16U
#define cJSON_Array 32U
#define cJSON_Object 64U

cJSON *cJSON_Parse(const char *value);
void cJSON_Delete(cJSON *item);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string);
int cJSON_GetArraySize(const cJSON *array);
cJSON *cJSON_GetArrayItem(const cJSON *array, int index);

cJSON *cJSON_CreateObject(void);
cJSON *cJSON_CreateArray(void);
cJSON *cJSON_CreateString(const char *string);
cJSON *cJSON_CreateNumber(double number);
cJSON *cJSON_CreateBool(bool boolean);
bool cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
bool cJSON_AddItemToArray(cJSON *array, cJSON *item);
cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *string);
cJSON *cJSON_AddNumberToObject(cJSON *object, const char *name, double number);
cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, bool boolean);
char *cJSON_PrintUnformatted(const cJSON *item);

#define cJSON_IsFalse(item) ((item) != NULL && (item)->type == cJSON_False)
#define cJSON_IsTrue(item) ((item) != NULL && (item)->type == cJSON_True)
#define cJSON_IsBool(item) (cJSON_IsFalse(item) || cJSON_IsTrue(item))
#define cJSON_IsNumber(item) ((item) != NULL && (item)->type == cJSON_Number)
#define cJSON_IsString(item) ((item) != NULL && (item)->type == cJSON_String && (item)->valuestring != NULL)
#define cJSON_IsArray(item) ((item) != NULL && (item)->type == cJSON_Array)
#define cJSON_IsObject(item) ((item) != NULL && (item)->type == cJSON_Object)

#endif
