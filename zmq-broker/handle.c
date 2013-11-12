/* handle.c - core flux_t handle operations */ 

#define _GNU_SOURCE
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <stdarg.h>
#include <json/json.h>
#include <czmq.h>

#include "hostlist.h"
#include "log.h"
#include "zmsg.h"
#include "util.h"

#include "flux.h"
#include "flux_handle.h"

static void *getimpl (flux_t h)
{
    return flux_aux_get (h, "handle_impl");
}

flux_t flux_handle_create (void *impl, FluxFreeFn *destroy, int flags)
{
    flux_t h = xzmalloc (sizeof (*h));

    h->flags = flags;
    if (!(h->aux = zhash_new()))
        oom ();
    flux_aux_set (h, impl, "handle_impl", destroy);

    return h;
}

void flux_handle_destroy (flux_t *hp)
{
    zhash_destroy (&(*hp)->aux);
    free (*hp);
    *hp = NULL;
}

void flux_flags_set (flux_t h, int flags)
{
    h->flags |= flags;
}

void flux_flags_unset (flux_t h, int flags)
{
    h->flags &= ~flags;
}

void *flux_aux_get (flux_t h, const char *name)
{
    return zhash_lookup (h->aux, name);
}

void flux_aux_set (flux_t h, const char *name, void *aux, FluxFreeFn *destroy)
{
    if (zhash_insert (h->aux, name, aux) < 0)
        oom ();
    zhash_freefn (h->aux, name, destroy);
}

int flux_request_sendmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->request_sendmsg) {
        errno = ENOSYS;
        return -1;
    }
    if (h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (*zmsg);

    return h->request_sendmsg (getimpl (h), zmsg);
}

int flux_response_recvmsg (flux_t h, zmsg_t **zmsg, bool nonblock)
{
    int rc;

    if (!h->response_recvmsg) {
        errno = ENOSYS;
        return -1;
    }
    rc = h->response_recvmsg (getimpl (h), zmsg, nonblock);
    if (h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (*zmsg);

    return rc;
}

int flux_response_putmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->response_putmsg) {
        errno = ENOSYS;
        return -1;
    }

    return h->response_putmsg (getimpl (h), zmsg);
}

int flux_event_sendmsg (flux_t h, zmsg_t **zmsg)
{
    if (!h->event_sendmsg) {
        errno = ENOSYS;
        return -1;
    }
    if (h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (*zmsg);

    return h->event_sendmsg (getimpl (h), zmsg);
}

int flux_event_recvmsg (flux_t h, zmsg_t **zmsg, bool nonblock)
{
    int rc;

    if (!h->event_recvmsg) {
        errno = ENOSYS;
        return -1;
    }
    rc = h->event_recvmsg (getimpl (h), zmsg, nonblock);
    if (h->flags & FLUX_FLAGS_TRACE)
        zmsg_dump_compact (*zmsg);

    return rc;
}

int flux_event_subscribe (flux_t h, const char *topic)
{
    if (!h->event_subscribe) {
        errno = ENOSYS;
        return -1;
    }

    return h->event_subscribe (getimpl (h), topic);
}

int flux_event_unsubscribe (flux_t h, const char *topic)
{
    if (!h->event_unsubscribe) {
        errno = ENOSYS;
        return -1;
    }

    return h->event_unsubscribe (getimpl (h), topic);
}

int flux_snoop_recvmsg (flux_t h, zmsg_t **zmsg, bool nonblock)
{
    if (!h->snoop_recvmsg) {
        errno = ENOSYS;
        return -1;
    }

    return h->snoop_recvmsg (getimpl (h), zmsg, nonblock);
}

int flux_snoop_subscribe (flux_t h, const char *topic)
{
    if (!h->snoop_subscribe) {
        errno = ENOSYS;
        return -1;
    }

    return h->snoop_subscribe (getimpl (h), topic);
}

int flux_snoop_unsubscribe (flux_t h, const char *topic)
{
    if (!h->snoop_unsubscribe) {
        errno = ENOSYS;
        return -1;
    }

    return h->snoop_unsubscribe (getimpl (h), topic);
}

int flux_rank (flux_t h)
{
    if (!h->rank) {
        errno = ENOSYS;
        return -1;
    }

    return h->rank (getimpl (h));
}

int flux_size (flux_t h)
{
    if (!h->size) {
        errno = ENOSYS;
        return -1;
    }

    return h->size (getimpl (h));
}

/**
 ** Higher level functions built on those above
 **/

int flux_event_send (flux_t h, json_object *request, const char *fmt, ...)
{
    zmsg_t *zmsg;
    char *tag;
    int rc;
    va_list ap;

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    zmsg = cmb_msg_encode (tag, request);
    free (tag);
    if ((rc = flux_event_sendmsg (h, &zmsg)) < 0)
        zmsg_destroy (&zmsg);
    return rc;
}

int flux_request_send (flux_t h, json_object *request, const char *fmt, ...)
{
    zmsg_t *zmsg;
    char *tag;
    int rc;
    va_list ap;
    json_object *empty = NULL;

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (!request)
        request = empty = util_json_object_new_object ();
    zmsg = cmb_msg_encode (tag, request);
    free (tag);
    if ((rc = flux_request_sendmsg (h, &zmsg)) < 0)
        zmsg_destroy (&zmsg);
    if (empty)
        json_object_put (empty);
    return rc;
}

int flux_response_recv (flux_t h, json_object **respp, char **tagp, bool nb)
{
    zmsg_t *zmsg;
    int rc = -1;

    if (flux_response_recvmsg (h, &zmsg, nb) < 0)
        goto done;
    if (cmb_msg_decode (zmsg, tagp, respp) < 0)
        goto done;
    rc = 0;
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    return rc;
}

json_object *flux_rpc (flux_t h, json_object *request, const char *fmt, ...)
{
    char *tag = NULL;
    json_object *response = NULL;
    zmsg_t *zmsg = NULL;
    zlist_t *nomatch = NULL;
    va_list ap;
    json_object *empty = NULL;

    if (!(nomatch = zlist_new ()))
        oom ();

    va_start (ap, fmt);
    if (vasprintf (&tag, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (!request)
        request = empty = util_json_object_new_object ();
    zmsg = cmb_msg_encode (tag, request);
    if (flux_request_sendmsg (h, &zmsg) < 0)
        goto done;
    do {
        if (flux_response_recvmsg (h, &zmsg, false) < 0)
            goto done;
        if (!cmb_msg_match (zmsg, tag)) {
            if (zlist_append (nomatch, zmsg) < 0)
                oom ();
        }
    } while (zmsg == NULL);
    if (cmb_msg_decode (zmsg, NULL, &response) < 0)
        goto done;
    if (!response) {
        json_object_put (response);
        response = NULL;
        goto done;
    }
    if (util_json_object_get_int (response, "errnum", &errno) == 0) {
        json_object_put (response);
        response = NULL;
        goto done;
    }
done:
    if (tag)
        free (tag);
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (empty)
        json_object_put (empty);
    if (nomatch) {
        while ((zmsg = zlist_pop (nomatch))) {
            if (flux_response_putmsg (h, &zmsg) < 0)
                zmsg_destroy (&zmsg);
        }
        zlist_destroy (&nomatch);
    }
    return response;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */