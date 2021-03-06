/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2011 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

/**
 * This file contains an implementation of the IO functions that should
 * work on Microsoft Windows. The current implementation use select(),
 * and the default implementation of select() doesn't support more than
 * 64 sockets. Since this is a quick'n'dirty prototype for Windows, I'm
 * not going to try to make it smarter (I want to reimplement it to use
 * IOCP anyway)
 *
 * @author Trond Norbye
 * @todo Rewrite to use IOCP
 */

#include "internal.h"
#include <libcouchbase/winsock_io_opts.h>

struct winsock_event {
    WSAEVENT event;
    SOCKET sock;
    short flags;
    void *cb_data;
    void (*handler)(libcouchbase_socket_t sock, short which, void *cb_data);
    struct winsock_event *next;
};

struct winsock_timer {
    int active;
    hrtime_t exptime;
    void *cb_data;
    void (*handler)(libcouchbase_socket_t sock, short which, void *cb_data);
};

struct winsock_io_cookie {
    struct winsock_event *events;
    struct winsock_timer *timer;

    fd_set readfds[FD_SETSIZE];
    fd_set writefds[FD_SETSIZE];
    fd_set exceptfds[FD_SETSIZE];

    int event_loop;
};

static void link_event(struct winsock_io_cookie *instance,
                       struct winsock_event *event)
{
    if (instance->events == NULL) {
        instance->events = event;
    } else {
        instance->events->next = event;
        event->next = NULL;
    }
}

static void unlink_event(struct winsock_io_cookie *instance,
                         struct winsock_event *event)
{
    if (instance->events == event) {
        instance->events = event->next;
    } else {
        struct winsock_event *prev = instance->events;
        struct winsock_event *next;
        for (next = prev->next; next != NULL; next = next->next) {
            if (event == next) {
                prev->next = next->next;
                return;
            }
        }
    }
}

static int getError()
{
    DWORD error = WSAGetLastError();
    switch (error) {
    case WSAECONNRESET:
        return ECONNRESET;
    case WSAEWOULDBLOCK:
        return EWOULDBLOCK;
    case WSAEINVAL:
        return EINVAL;
    case WSAEINPROGRESS:
        return EINPROGRESS;
    case WSAEALREADY:
        return EALREADY;
    case WSAEISCONN:
        return EISCONN;
    case WSAENOTCONN:
        return ENOTCONN;
    case WSAECONNREFUSED:
        return ECONNREFUSED;

    default:
        fprintf(stdout, "Unknown error code: %u\n", error);
        abort();
        return EINVAL;
    }

    return EINVAL;
}

static libcouchbase_ssize_t libcouchbase_io_recv(struct libcouchbase_io_opt_st *iops,
                                                 libcouchbase_socket_t sock,
                                                 void *buffer,
                                                 libcouchbase_size_t len,
                                                 int flags)
{
    DWORD fl = 0;
    DWORD nr;
    WSABUF wsabuf = { len, buffer };
    (void)flags;

    if (WSARecv(sock, &wsabuf, 1, &nr, &fl, NULL, NULL) == SOCKET_ERROR) {
        iops->error = getError();

        // recv on a closed socket should return 0
        if (iops->error == ECONNRESET) {
            return 0;
        }
        return -1;
    }

    return (libcouchbase_ssize_t)nr;
}

static libcouchbase_ssize_t libcouchbase_io_recvv(struct libcouchbase_io_opt_st *iops,
                                                  libcouchbase_socket_t sock,
                                                  struct libcouchbase_iovec_st *iov,
                                                  libcouchbase_size_t niov)
{
    DWORD fl = 0;
    DWORD nr;
    WSABUF wsabuf[2];

    assert(niov == 2);
    wsabuf[0].buf = iov[0].iov_base;
    wsabuf[0].len = iov[0].iov_len;
    wsabuf[1].buf = iov[1].iov_base;
    wsabuf[1].len = iov[1].iov_len;

    if (WSARecv(sock, wsabuf, iov[1].iov_len ? 2 : 1,
                &nr, &fl, NULL, NULL) == SOCKET_ERROR) {
        iops->error = getError();

        // recv on a closed socket should return 0
        if (iops->error == ECONNRESET) {
            return 0;
        }
        return -1;
    }

    return (libcouchbase_ssize_t)nr;
}


static libcouchbase_ssize_t libcouchbase_io_send(struct libcouchbase_io_opt_st *iops,
                                                 libcouchbase_socket_t sock,
                                                 const void *msg,
                                                 libcouchbase_size_t len,
                                                 int flags)
{
    DWORD fl = 0;
    DWORD nw;
    WSABUF wsabuf = { len, (char *)msg };
    (void)flags;

    if (WSASend(sock, &wsabuf, 1, &nw, fl, NULL, NULL) == SOCKET_ERROR) {
        iops->error = getError();
        return -1;
    }

    return (libcouchbase_ssize_t)nw;
}

static libcouchbase_ssize_t libcouchbase_io_sendv(struct libcouchbase_io_opt_st *iops,
                                                  libcouchbase_socket_t sock,
                                                  struct libcouchbase_iovec_st *iov,
                                                  libcouchbase_size_t niov)
{
    DWORD fl = 0;
    DWORD nw;
    WSABUF wsabuf[2];

    assert(niov == 2);
    wsabuf[0].buf = iov[0].iov_base;
    wsabuf[0].len = iov[0].iov_len;
    wsabuf[1].buf = iov[1].iov_base;
    wsabuf[1].len = iov[1].iov_len;

    if (WSASend(sock, wsabuf, iov[1].iov_len ? 2 : 1,
                &nw, fl, NULL, NULL) == SOCKET_ERROR) {
        iops->error = getError();
        return -1;
    }

    return (libcouchbase_ssize_t)nw;
}

static libcouchbase_socket_t libcouchbase_io_socket(struct libcouchbase_io_opt_st *iops,
                                                    int domain,
                                                    int type,
                                                    int protocol)
{
    libcouchbase_socket_t sock = WSASocket(domain, type, protocol, NULL, 0, 0);
    if (sock == INVALID_SOCKET) {
        iops->error = getError();
    } else {
        u_long noblock = 1;
        if (ioctlsocket(sock, FIONBIO, &noblock) == SOCKET_ERROR) {
            iops->error = getError();
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
    }

    return sock;
}

static void libcouchbase_io_close(struct libcouchbase_io_opt_st *iops,
                                  libcouchbase_socket_t sock)
{
    (void)iops;
    closesocket(sock);
}

static int libcouchbase_io_connect(struct libcouchbase_io_opt_st *iops,
                                   libcouchbase_socket_t sock,
                                   const struct sockaddr *name,
                                   unsigned int namelen)
{
    int ret = WSAConnect(sock, name, namelen, NULL, NULL, NULL, NULL);
    if (ret == SOCKET_ERROR) {
        iops->error = getError();
    }
    return ret;
}

static void *libcouchbase_io_create_event(struct libcouchbase_io_opt_st *iops)
{
    struct winsock_event *ret = calloc(1, sizeof(*ret));
    if (ret != NULL) {
        link_event(iops->cookie, ret);
    }

    return ret;
}

static int libcouchbase_io_update_event(struct libcouchbase_io_opt_st *iops,
                                        libcouchbase_socket_t sock,
                                        void *event,
                                        short flags,
                                        void *cb_data,
                                        void (*handler)(libcouchbase_socket_t sock,
                                                        short which,
                                                        void *cb_data))
{
    int mask = 0;
    struct winsock_event *ev = event;
    ev->sock = sock;
    ev->handler = handler;
    ev->cb_data = cb_data;
    ev->flags = flags;
    return 0;
}

static void libcouchbase_io_destroy_event(struct libcouchbase_io_opt_st *iops,
                                          void *event)
{
    struct winsock_event *ev = event;
    unlink_event(iops->cookie, event);
    free(ev);
}

static void libcouchbase_io_delete_event(struct libcouchbase_io_opt_st *iops,
                                         libcouchbase_socket_t sock,
                                         void *event)
{
    struct winsock_event *ev = event;
    ev->flags = 0;
    ev->cb_data = NULL;
    ev->handler = NULL;
}

void *libcouchbase_io_create_timer(struct libcouchbase_io_opt_st *iops)
{
    struct winsock_io_cookie *me = iops->cookie;
    struct winsock_timer *ret;

    assert(me->timer == NULL);
    ret = calloc(1, sizeof(*ret));
    if (ret != NULL) {
        me->timer = ret;
    }

    return ret;
}

void libcouchbase_io_destroy_timer(struct libcouchbase_io_opt_st *iops,
                                   void *timer)
{
    struct winsock_io_cookie *me = iops->cookie;
    assert(me->timer == timer);
    free(timer);
    me->timer = NULL;
}

void libcouchbase_io_delete_timer(struct libcouchbase_io_opt_st *iops,
                                  void *timer)
{
    struct winsock_io_cookie *me = iops->cookie;
    assert(me->timer == timer);
    me->timer->active = 0;

}

int libcouchbase_io_update_timer(struct libcouchbase_io_opt_st *iops,
                                 void *timer,
                                 libcouchbase_uint32_t usec,
                                 void *cb_data,
                                 void (*handler)(libcouchbase_socket_t sock,
                                                 short which,
                                                 void *cb_data))
{
    struct winsock_io_cookie *me = iops->cookie;
    assert(me->timer == timer);
    me->timer->exptime = gethrtime() + (usec * (hrtime_t)1000);
    me->timer->cb_data = cb_data;
    me->timer->handler = handler;
    me->timer->active = 1;
    return 0;
}

static void libcouchbase_io_stop_event_loop(struct libcouchbase_io_opt_st *iops)
{
    struct winsock_io_cookie *instance = iops->cookie;
    instance->event_loop = 0;
}

static void libcouchbase_io_run_event_loop(struct libcouchbase_io_opt_st *iops)
{
    struct winsock_io_cookie *instance = iops->cookie;
    int nevents;
    struct winsock_event *n;

    instance->event_loop = 1;
    do {
        struct timeval tmo, *t;
        int ret;

        FD_ZERO(instance->readfds);
        FD_ZERO(instance->writefds);
        FD_ZERO(instance->exceptfds);
        nevents = 0;

        for (n = instance->events; n != NULL; n = n->next) {
            if (n->flags != 0) {
                if (n->flags & LIBCOUCHBASE_READ_EVENT) {
                    FD_SET(n->sock, instance->readfds);
                }

                if (n->flags & LIBCOUCHBASE_WRITE_EVENT) {
                    FD_SET(n->sock, instance->writefds);
                }
                ++nevents;
            }
        }

        if (nevents == 0) {
            instance->event_loop = 0;
            return;
        }

        if (instance->timer != NULL && instance->timer->active) {
            hrtime_t now = gethrtime();
            tmo.tv_sec = 0;
            tmo.tv_usec = 0;

            if (now < instance->timer->exptime) {
                hrtime_t delta = instance->timer->exptime - now;
                delta /= 1000;
                tmo.tv_sec = (long)(delta / 1000000);
                tmo.tv_usec = delta % 1000000;
            }
            t = &tmo;
        } else {
            t = NULL;
        }
        ret = select(FD_SETSIZE, instance->readfds, instance->writefds,
                     instance->exceptfds, t);

        if (ret == SOCKET_ERROR) {
            fprintf(stderr, "ERROR!!!\n");
            return ;
        }

        if (ret == 0) {
            instance->timer->handler(-1, 0, instance->timer->cb_data);
        } else {
            for (n = instance->events; n != NULL; n = n->next) {
                if (n->flags != 0) {
                    short flags = 0;

                    if (FD_ISSET(n->sock, instance->readfds)) {
                        flags |= LIBCOUCHBASE_READ_EVENT;
                    }

                    if (FD_ISSET(n->sock, instance->writefds)) {
                        flags |= LIBCOUCHBASE_WRITE_EVENT;
                    }

                    if (flags != 0) {
                        n->handler(n->sock, flags, n->cb_data);
                    }
                }
            }
        }
    } while (instance->event_loop);
}

static void libcouchbase_destroy_io_opts(struct libcouchbase_io_opt_st *instance)
{
    free(instance->cookie);
    free(instance);
}


LIBCOUCHBASE_API
struct libcouchbase_io_opt_st *libcouchbase_create_winsock_io_opts(void) {
    struct libcouchbase_io_opt_st *ret;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 0), &wsaData) != 0) {
        fprintf(stderr, "Socket Initialization Error. Program aborted\n");
        return NULL;
    }

    if ((ret = calloc(1, sizeof(*ret))) == NULL) {
        return NULL;
    }

    // setup io iops!
    ret->recv = libcouchbase_io_recv;
    ret->send = libcouchbase_io_send;
    ret->recvv = libcouchbase_io_recvv;
    ret->sendv = libcouchbase_io_sendv;
    ret->socket = libcouchbase_io_socket;
    ret->close = libcouchbase_io_close;
    ret->connect = libcouchbase_io_connect;
    ret->delete_event = libcouchbase_io_delete_event;
    ret->destroy_event = libcouchbase_io_destroy_event;
    ret->create_event = libcouchbase_io_create_event;
    ret->update_event = libcouchbase_io_update_event;
    ret->delete_timer = libcouchbase_io_delete_timer;
    ret->destroy_timer = libcouchbase_io_destroy_timer;
    ret->create_timer = libcouchbase_io_create_timer;
    ret->update_timer = libcouchbase_io_update_timer;
    ret->run_event_loop = libcouchbase_io_run_event_loop;
    ret->stop_event_loop = libcouchbase_io_stop_event_loop;
    ret->destructor = libcouchbase_destroy_io_opts;
    ret->cookie = calloc(1, sizeof(struct winsock_io_cookie));

    if (ret->cookie == NULL) {
        free(ret);
        ret = NULL;
    }

    return ret;
}

LIBCOUCHBASE_API
struct libcouchbase_io_opt_st *libcouchbase_create_test_loop(void) {
    return libcouchbase_create_winsock_io_opts();
}
