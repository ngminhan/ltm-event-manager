#include "auth.h"

static const struct {
    const char *code, *message;
} errors[] = {[BAD] = {"BAD_REQUEST", "Invalid command or parameters."},
              [AUTH] = {"AUTH_REQUIRED", "Please log in first."},
              [INVALID] = {"INVALID_CREDENTIALS", "Invalid username or password."},
              [DENIED] = {"FORBIDDEN", "You do not have permission to perform this action."},
              [MISSING] = {"NOT_FOUND", "Account, event, or waitlist entry not found."},
              [CONFLICT] = {"CONFLICT", "The account already exists or the request conflicts with the current participation status."},
              [INTERNAL] = {"INTERNAL_ERROR", "The server could not process the request."}};

bool init_store(sqlite3 *db) {
    return sqlite3_exec(
               db,
               "PRAGMA foreign_keys=ON;"
               "CREATE TABLE IF NOT EXISTS users(name TEXT PRIMARY KEY,salt BLOB NOT NULL,hash "
               "BLOB NOT NULL);"
               "CREATE TABLE IF NOT EXISTS events(id INTEGER PRIMARY KEY,owner TEXT NOT NULL "
               "REFERENCES users(name),"
               "starts_at TEXT NOT NULL,location TEXT NOT NULL,description TEXT NOT NULL);"
               "CREATE TABLE IF NOT EXISTS participation(event INTEGER REFERENCES events(id),"
               "member TEXT REFERENCES users(name),state TEXT NOT NULL CHECK(state IN "
               "('invited','requested','joined')),"
               "PRIMARY KEY(event,member));"
               "CREATE TABLE IF NOT EXISTS creations(actor TEXT, token TEXT,event INTEGER REFERENCES events(id),"
               "PRIMARY KEY(actor,token));",
               NULL, NULL, NULL) == SQLITE_OK;
}

bool valid_text(const char *s, size_t limit) {
    if (!s || !*s || strlen(s) > limit)
        return false;
    for (; *s; s++)
        if ((unsigned char)*s < 32)
            return false;
    return true;
}

sqlite3_stmt *prepare(Action *a, const char *sql) {
    sqlite3_stmt *s = NULL;
    if (sqlite3_prepare_v2(a->db, sql, -1, &s, NULL) != SQLITE_OK)
        return NULL;
    for (int i = 1; i <= sqlite3_bind_parameter_count(s); i++) {
        const char *parameter = sqlite3_bind_parameter_name(s, i);
        if (!parameter)
            continue;
        const char *key = parameter + 1;
        int rc;
        if (strcmp(key, "event") == 0)
            rc = sqlite3_bind_int64(s, i, a->event);
        else {
            const char *value =
                strcmp(key, "actor") == 0 ? a->session->username : field(a->request, key);
            rc = sqlite3_bind_text(s, i, value, -1, SQLITE_STATIC);
        }
        if (rc != SQLITE_OK) {
            sqlite3_finalize(s);
            return NULL;
        }
    }
    return s;
}

Result finish_write(sqlite3_stmt *s) {
    int rc = sqlite3_step(s);
    sqlite3_finalize(s);
    if (rc == SQLITE_CONSTRAINT)
        return CONFLICT;
    return rc == SQLITE_DONE ? OK : INTERNAL;
}

static Result execute(Action *a, const AuthWork *work) {
    if (!field(a->request, "type") || strcmp(field(a->request, "type"), "request") || !a->op)
        return BAD;
    if (is_op(a, "LOGIN") || is_op(a, "REGISTER"))
        return finish_auth(a, work);
    bool member_op = is_op(a, "INVITE") || is_op(a, "APPROVE_JOIN") || is_op(a, "JOIN_REQUEST") ||
                     is_op(a, "ACCEPT_INVITE");
    if (!member_op && !is_op(a, "CREATE_EVENT") && !is_op(a, "LIST_EVENTS") &&
        !is_op(a, "LIST_PENDING") && !is_op(a, "LIST_PARTICIPANTS"))
        return BAD;
    if (!*a->session->username)
        return AUTH;
    if (member_op)
        return participation(a);
    if (is_op(a, "CREATE_EVENT"))
        return create_event(a);
    return list_page(a);

}

cJSON *dispatch(sqlite3 *db, Session *session, const cJSON *request, Notify notify, const AuthWork *work) {
    int64_t id = positive_id(request, "id");
    if (!id)
        return NULL;
    Action action = {db,
                     session,
                     request,
                     cJSON_CreateObject(),
                     notify,
                     field(request, "op"),
                     field(request, "username"),
                     positive_id(request, "event_id")};
    Result result = execute(&action, work);
    cJSON *reply = cJSON_CreateObject();
    cJSON_AddStringToObject(reply, "type", "response");
    cJSON_AddNumberToObject(reply, "id", (double)id);
    if (result == OK) {
        cJSON_AddItemToObject(reply, "data", action.data);
        char *serialized = cJSON_PrintUnformatted(reply);
        if (!serialized || strlen(serialized) + 10 > FRAME_LIMIT) {
            cJSON_DeleteItemFromObjectCaseSensitive(reply, "data");
            result = INTERNAL;
        }
        cJSON_free(serialized);
    } else
        cJSON_Delete(action.data);
    cJSON_AddBoolToObject(reply, "ok", result == OK);
    if (result != OK) {
        cJSON *error = cJSON_AddObjectToObject(reply, "error");
        cJSON_AddStringToObject(error, "code", errors[result].code);
        cJSON_AddStringToObject(error, "message", errors[result].message);
    }
    return reply;
}
