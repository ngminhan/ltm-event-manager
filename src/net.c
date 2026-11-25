#include "net.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int tcp_socket(const char *host, const char *port, bool listener) {
    struct addrinfo hints = {
        .ai_family = AF_INET, .ai_socktype = SOCK_STREAM, .ai_flags = listener ? AI_PASSIVE : 0};
    struct addrinfo *addresses;
    int status = getaddrinfo(host, port, &hints, &addresses);
    if (status) {
        fprintf(stderr, "%s\n", gai_strerror(status));
        return -1;
    }
    int fd = -1;
    for (struct addrinfo *a = addresses; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0)
            continue;
        if (!listener) {
            if (connect(fd, a->ai_addr, a->ai_addrlen) == 0)
                break;
        } else {
            int yes = 1;
            if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == 0 &&
                bind(fd, a->ai_addr, a->ai_addrlen) == 0 && listen(fd, 64) == 0)
                break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd >= 0 && !nonblocking(fd)) {
        close(fd);
        return -1;
    }
    return fd;
}

const char *field(const cJSON *object, const char *name) {
    return cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(object, name));
}

int64_t positive_id(const cJSON *object, const char *name) {
    double n = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(object, name));
    return n >= 1 && n <= MAX_JSON_ID && n == (int64_t)n ? (int64_t)n : 0;
}

static bool temporary(void) { return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK; }

/* cJSON exposes NUL-terminated strings, so an embedded NUL cannot round-trip. */
bool valid_json_text(const unsigned char *p, size_t len) {
    const unsigned char *end = p + len;
    while (p < end) {
        unsigned char lead = *p++;
        if (lead == 0)
            return false;
        if (lead == '\\' && p < end) {
            if (*p == '\\') {
                p++;
                continue;
            }
            if (end - p >= 5 && memcmp(p, "u0000", 5) == 0)
                return false;
        }
        if (lead < 0x80)
            continue;
        if (lead < 0xc2 || lead > 0xf4)
            return false;
        int count = lead < 0xe0 ? 1 : lead < 0xf0 ? 2 : 3;
        if (end - p < count || (lead == 0xe0 && *p < 0xa0) || (lead == 0xed && *p >= 0xa0) ||
            (lead == 0xf0 && *p < 0x90) || (lead == 0xf4 && *p >= 0x90))
            return false;
        while (count--)
            if ((*p++ & 0xc0) != 0x80)
                return false;
    }
    return true;
}

bool decode_frames(Channel *c, MessageHandler handle, void *context) {
    size_t used = 0;
    while (!c->paused && c->received - used >= 4) {
        uint32_t header;
        memcpy(&header, c->input + used, 4);
        size_t length = ntohl(header);
        if (!length || length > FRAME_LIMIT)
            return false;
        if (c->received - used - 4 < length)
            break;
        const char *body = (const char *)c->input + used + 4;
        if (!valid_json_text((const unsigned char *)body, length))
            return false;
        const char *end = body;
        cJSON *json = cJSON_ParseWithLengthOpts(body, length, &end, false);
        while (end < body + length && strchr(" \r\t\n", *end) && *end)
            end++;
        bool ok = cJSON_IsObject(json) && end == body + length && handle(context, json);
        cJSON_Delete(json);
        if (!ok)
            return false;
        used += length + 4;
    }
    c->received -= used;
    memmove(c->input, c->input + used, c->received);
    return true;
}

bool receive_frames(int fd, Channel *c, MessageHandler handle, void *context) {
    ssize_t n = recv(fd, c->input + c->received, sizeof c->input - c->received, 0);
    if (n <= 0)
        return n < 0 && temporary();
    c->received += (size_t)n;
    return decode_frames(c, handle, context);
}

bool queue_json(Channel *c, const cJSON *message) {
    char *json = cJSON_PrintUnformatted(message);
    if (!json)
        return false;
    size_t length = strlen(json), pending = c->queued - c->sent;
    bool fits = length <= FRAME_LIMIT && pending + length + 4 <= QUEUE_LIMIT;
    if (fits && c->sent)
        memmove(c->output, c->output + c->sent, pending);
    unsigned char *buffer = fits ? realloc(c->output, pending + length + 4) : NULL;
    if (buffer) {
        uint32_t header = htonl((uint32_t)length);
        memcpy(buffer + pending, &header, 4);
        memcpy(buffer + pending + 4, json, length);
        c->output = buffer;
        c->queued = pending + length + 4;
        c->sent = 0;
    }
    cJSON_free(json);
    return buffer != NULL;
}

bool flush_frames(int fd, Channel *c) {
    if (c->queued == c->sent)
        return true;
    ssize_t n = send(fd, c->output + c->sent, c->queued - c->sent, 0);
    if (n <= 0)
        return n < 0 && temporary();
    c->sent += (size_t)n;
    if (c->sent == c->queued)
        c->sent = c->queued = 0;
    return true;
}
