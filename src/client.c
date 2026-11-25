#include "net.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

typedef struct {
    const char *op, *keys[4], *usage;
    int count;
    bool optional_last;
} Command;
static const Command commands[] = {
    {"REGISTER", {"username", "password"}, "username | password", 2, false},
    {"LOGIN", {"username", "password"}, "username | password", 2, false},
    {"CREATE_EVENT",
     {"starts_at", "location", "description", "creation_key"},
     "starts_at | location | description [| creation_key]",
     4, true},
    {"LIST_PARTICIPANTS", {"event_id", "after"}, "event_id [| after]", 2, true},
    {"LIST_EVENTS", {"after"}, "[after]", 1, true},
    {"INVITE", {"event_id", "username"}, "event_id | username", 2, false},
    {"ACCEPT_INVITE", {"event_id"}, "event_id", 1, false},
    {"JOIN_REQUEST", {"event_id"}, "event_id", 1, false},
    {"APPROVE_JOIN", {"event_id", "username"}, "event_id | username", 2, false},
    {"LIST_PENDING", {"after"}, "[after]", 1, true}};
typedef struct {
    Channel channel;
    int next_id, pending;
    bool done;
} Client;

static void help(void) {
    for (size_t i = 0; i < sizeof commands / sizeof *commands; i++)
        printf("%-14s %s\n", commands[i].op, commands[i].usage);
    puts("HELP\nQUIT");
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = 0;
    return s;
}

static bool add_args(cJSON *request, const Command *cmd, char *args) {
    if (!strncmp(args, "--json ", 7)) {
        const char *json = args + 7;
        if (!valid_json_text((const unsigned char *)json, strlen(json)))
            return false;
        cJSON *array = cJSON_ParseWithOpts(json, NULL, true);
        int count = cJSON_GetArraySize(array);
        bool ok = cJSON_IsArray(array) && count >= cmd->count - cmd->optional_last && count <= cmd->count;
        for (int i = 0; ok && i < count; i++) {
            cJSON *value = cJSON_DetachItemFromArray(array, 0);
            ok = cJSON_AddItemToObject(request, cmd->keys[i], value);
            if (!ok)
                cJSON_Delete(value);
        }
        cJSON_Delete(array);
        return ok;
    }
    if (!cmd->count)
        return !*args;
    for (int i = 0; i < cmd->count; i++) {
        if ((!args || !*args) && cmd->optional_last && i == cmd->count - 1)
            return true;
        char *next = strchr(args, '|');
        if ((next && i + 1 == cmd->count) || (!next && i + 1 < cmd->count - cmd->optional_last))
            return false;
        if (next)
            *next++ = 0;
        char *value = trim(args);
        if (!*value)
            return false;
        if ((!strcmp(cmd->keys[i], "event_id") || !strcmp(cmd->keys[i], "after"))) {
            char *end;
            errno = 0;
            long long n = strtoll(value, &end, 10);
            if (*end || errno || n <= 0 || n > MAX_JSON_ID)
                return false;
            cJSON_AddNumberToObject(request, cmd->keys[i], (double)n);
        } else
            cJSON_AddStringToObject(request, cmd->keys[i], value);
        args = next;
    }
    return true;
}

static void command(Client *c, char *line) {
    char *op = trim(line);
    char *args = op + strcspn(op, " \t");
    if (*args)
        *args++ = 0;
    args = trim(args);
    if (!*op)
        return;
    if (!strcasecmp(op, "HELP")) {
        help();
        return;
    }
    if (!strcasecmp(op, "QUIT")) {
        c->done = true;
        return;
    }
    for (size_t i = 0; i < sizeof commands / sizeof *commands; i++) {
        const Command *cmd = &commands[i];
        if (strcasecmp(op, cmd->op))
            continue;
        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "type", "request");
        cJSON_AddStringToObject(req, "op", cmd->op);
        cJSON_AddNumberToObject(req, "id", c->next_id);
        if (!add_args(req, cmd, args))
            fprintf(stderr, "Usage: %s %s\n", cmd->op, cmd->usage);
        else if (queue_json(&c->channel, req)) {
            c->pending = c->next_id;
            c->next_id = c->next_id == INT_MAX ? 1 : c->next_id + 1;
        } else
            fputs("Could not send the command.\n", stderr);
        cJSON_Delete(req);
        return;
    }
    fputs("Invalid command. Type HELP to see the command syntax.\n", stderr);
}

static bool display(void *context, const cJSON *message) {
    Client *client = context;
    const char *type = field(message, "type");
    if (!type)
        return false;
    if (!strcmp(type, "response")) {
        if (!client->pending || positive_id(message, "id") != client->pending)
            return false;
        client->pending = 0;
    } else if (strcmp(type, "notification"))
        return false;
    char *json = cJSON_PrintUnformatted(message);
    if (!json)
        return false;
    puts(json);
    fflush(stdout);
    cJSON_free(json);
    return true;
}

static int run(int fd) {
    Client c = {.next_id = 1};
    char line[FRAME_LIMIT + 1];
    size_t length = 0;
    bool overflow = false;
    int status = 0;
    while (!c.done || c.pending) {
        struct pollfd fds[] = {{.fd = fd, .events = POLLIN | (c.channel.queued ? POLLOUT : 0)},
                               {.fd = c.pending || c.done ? -1 : STDIN_FILENO, .events = POLLIN}};
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            status = 1;
            break;
        }
        if ((fds[0].revents & (POLLERR | POLLNVAL)) ||
            ((fds[0].revents & (POLLIN | POLLHUP)) &&
             !receive_frames(fd, &c.channel, display, &c)) ||
            ((fds[0].revents & POLLOUT) && !flush_frames(fd, &c.channel))) {
            fputs("Connection closed.\n", stderr);
            status = 1;
            break;
        }
        if (fds[1].revents & (POLLERR | POLLNVAL)) {
            status = 1;
            break;
        }
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            char ch;
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0) {
                status = 1;
                break;
            }
            if (!n || ch == '\n') {
                line[length] = 0;
                if (overflow)
                    fputs("Command too long.\n", stderr);
                else
                    command(&c, line);
                length = 0;
                overflow = false;
                if (!n)
                    c.done = true;
            } else if (length < FRAME_LIMIT)
                line[length++] = ch;
            else
                overflow = true;
        }
    }
    free(c.channel.output);
    return status;
}

int main(int argc, char **argv) {
    if (argc > 3) {
        fprintf(stderr, "Usage: client [host [port]]\n");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    int fd = tcp_socket(argc > 1 ? argv[1] : "127.0.0.1", argc > 2 ? argv[2] : "5050", false);
    if (fd < 0) {
        perror("connect");
        return 1;
    }
    if (isatty(STDIN_FILENO))
        help();
    int status = run(fd);
    close(fd);
    return status;
}
