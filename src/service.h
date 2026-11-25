#ifndef EVENT_SERVICE_H
#define EVENT_SERVICE_H
#include "net.h"
#include <sqlite3.h>
enum { USER_LIMIT = 64 };
typedef struct {
    char username[USER_LIMIT + 1];
} Session;
typedef void (*Notify)(const char *recipient, const cJSON *message);
bool init_store(sqlite3 *db);
struct AuthWork;
cJSON *dispatch(sqlite3 *db, Session *session, const cJSON *request, Notify notify, const struct AuthWork *work);
#endif
