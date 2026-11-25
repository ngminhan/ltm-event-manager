#include "auth.h"
#include <pthread.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

enum { MAX_CLIENTS = 128 };
typedef struct {
    Channel channel;
    Session session;
    bool closing, eof;
    int slot;
    AuthWork *auth;
} Peer;

static Peer *peers[MAX_CLIENTS + 1];
enum { WORKER = MAX_CLIENTS + 1 };
static struct pollfd sockets[WORKER + 1];
static int jobs[2];
static unsigned outstanding;
static sqlite3 *database;

static void notify_user(const char *recipient, const cJSON *message) {
    for (int i = 1; i <= MAX_CLIENTS; i++) {
        Peer *peer = peers[i];
        if (peer && !peer->closing && strcmp(peer->session.username, recipient) == 0)
            peer->closing = !queue_json(&peer->channel, message);
    }
}

static bool reply_to(Peer *peer, const cJSON *request, const AuthWork *work) {
    cJSON *reply = dispatch(database, &peer->session, request, notify_user, work);
    bool ok = reply && queue_json(&peer->channel, reply);
    if (reply && !ok)
        peer->closing = true;
    cJSON_Delete(reply);
    return ok;
}

static ssize_t transfer(int fd, AuthWork **work, bool writing) {
    ssize_t n;
    do {
        n = writing ? send(fd, work, sizeof *work, 0) : recv(fd, work, sizeof *work, 0);
    } while (n < 0 && errno == EINTR);
    return n;
}

static void *hash_worker(void *unused) {
    (void)unused;
    AuthWork *work;
    while (transfer(jobs[1], &work, false) == sizeof work) {
        hash_password(work);
        if (transfer(jobs[1], &work, true) != sizeof work)
            break;
    }
    return NULL;
}

static bool respond(void *context, const cJSON *request) {
    Peer *peer = context;
    const char *op = field(request, "op"), *type = field(request, "type");
    if (!op || !type || strcmp(type, "request") || !positive_id(request, "id") ||
        (strcmp(op, "LOGIN") && strcmp(op, "REGISTER")))
        return reply_to(peer, request, NULL);
    AuthWork *work = calloc(1, sizeof *work);
    if (!work)
        return reply_to(peer, request, NULL);
    work->result = prepare_auth(work, database, &peer->session, request);
    if (work->result == OK) {
        work->slot = peer->slot;
        work->request = cJSON_Duplicate(request, true);
        if (work->request && outstanding < MAX_CLIENTS &&
            transfer(jobs[0], &work, true) == sizeof work) {
            peer->auth = work;
            peer->channel.paused = true;
            outstanding++;
            return true;
        }
        work->result = INTERNAL;
    }
    bool ok = reply_to(peer, request, work);
    free_auth(work);
    return ok;
}

static void finish_job(void) {
    AuthWork *work;
    if (transfer(jobs[0], &work, false) != sizeof work)
        return;
    outstanding--;
    Peer *peer = peers[work->slot];
    if (peer && peer->auth == work) {
        peer->auth = NULL;
        peer->channel.paused = false;
        if (!reply_to(peer, work->request, work))
            peer->closing = true;
        else if (!decode_frames(&peer->channel, respond, peer))
            peer->eof = true;
    }
    free_auth(work);
}

static void accept_peer(void) {
    int fd = accept(sockets[0].fd, NULL, NULL);
    if (fd < 0)
        return;
    for (int i = 1; i <= MAX_CLIENTS; i++) {
        if (peers[i])
            continue;
        if (!nonblocking(fd) || !(peers[i] = calloc(1, sizeof(Peer))))
            break;
        peers[i]->slot = i;
        sockets[i] = (struct pollfd){.fd = fd, .events = POLLIN};
        return;
    }
    close(fd);
}

static void disconnect(int i) {
    close(sockets[i].fd);
    sockets[i].fd = -1;
    free(peers[i]->channel.output);
    free(peers[i]);
    peers[i] = NULL;
}

static int serve(void) {
    for (;;) {
        int ready = poll(sockets, WORKER + 1, -1);
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready < 0 || sockets[0].revents & (POLLERR | POLLNVAL | POLLHUP))
            return 1;
        if (sockets[WORKER].revents & POLLIN)
            finish_job();
        if (sockets[0].revents & POLLIN)
            accept_peer();
        for (int i = 1; i <= MAX_CLIENTS; i++) {
            Peer *p = peers[i];
            if (!p)
                continue;
            short events = sockets[i].revents;
            if (events & (POLLERR | POLLNVAL))
                p->closing = true;
            if (!p->closing && !p->eof && !p->auth && events & (POLLIN | POLLHUP)) {
                if (!receive_frames(sockets[i].fd, &p->channel, respond, p))
                    p->eof = true;
            }
            if (events & POLLOUT && !flush_frames(sockets[i].fd, &p->channel))
                p->closing = true;
            if (p->closing || (p->eof && !p->channel.queued))
                disconnect(i);
        }
        
        for (int i = 1; i <= MAX_CLIENTS; i++) {
            if (peers[i] && peers[i]->closing)
                disconnect(i);
            if (peers[i])
                sockets[i].events =
                    (peers[i]->eof || peers[i]->auth ? 0 : POLLIN) | (peers[i]->channel.queued ? POLLOUT : 0);
        }
    }
}

int main(int argc, char **argv) {
    if (argc > 4) {
        fprintf(stderr, "Usage: server [port [database [bind_address]]]\n");
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);
    if (sqlite3_open(argc > 2 ? argv[2] : "events.db", &database) != SQLITE_OK ||
        !init_store(database)) {
        fprintf(stderr, "SQLite: %s\n", sqlite3_errmsg(database));
        sqlite3_close(database);
        return 1;
    }
    for (int i = 1; i <= MAX_CLIENTS; i++)
        sockets[i].fd = -1;
    const char *port = argc > 1 ? argv[1] : "5050";
    const char *address = argc > 3 ? argv[3] : "127.0.0.1";
    sockets[0] = (struct pollfd){.fd = tcp_socket(address, port, true), .events = POLLIN};
    if (sockets[0].fd < 0) {
        perror("listen");
        sqlite3_close(database);
        return 1;
    }
    fprintf(stderr, "Event Manager TCP port %s\n", port);
    pthread_t thread;
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, jobs) || !nonblocking(jobs[0]) ||
        pthread_create(&thread, NULL, hash_worker, NULL)) {
        fprintf(stderr, "Could not start authentication worker.\n");
        return 1;
    }
    sockets[WORKER] = (struct pollfd){.fd = jobs[0], .events = POLLIN};
    int result = serve();
    for (int i = 1; i <= MAX_CLIENTS; i++)
        if (peers[i])
            disconnect(i);
    close(sockets[0].fd);
    sqlite3_close(database);
    return result;
}
