#ifndef EVENT_ACTION_H
#define EVENT_ACTION_H
#include "service.h"
#include <string.h>
typedef enum { OK, BAD, AUTH, INVALID, DENIED, MISSING, CONFLICT, INTERNAL } Result;
typedef struct {
    sqlite3 *db;
    Session *session;
    const cJSON *request;
    cJSON *data;
    Notify notify;
    const char *op, *target;
    int64_t event;
} Action;

static inline bool is_op(Action *a, const char *name) { return strcmp(a->op, name) == 0; }

bool valid_text(const char *s, size_t limit);
sqlite3_stmt *prepare(Action *a, const char *sql);
Result finish_write(sqlite3_stmt *s);
Result create_event(Action *a);
Result participation(Action *a);
Result list_page(Action *a);
#endif
