// For now just a copy of quicperf.c

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "picoquic.h"
#include "picoquic_utils.h"
#include "picosplay.h"
#include "unibo_quicperf.h"

/* management of scenarios by the quicperf client 
 */

size_t unibo_quicperf_parse_nb_stream(char const* text) {
    size_t n = 0;
    int after_semi = 1;

    while (*text != 0) {
        if (*text++ == ';') {
            n++;
            after_semi = 0;
        }
        else {
            after_semi = 1;
        }
    }

    n += after_semi;

    return n;
}

char const* unibo_quicperf_parse_stream_spaces(char const* text) {
    if (text != NULL) {
        while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
            text++;
        }
    }
    return text;
}

char const* unibo_quicperf_parse_number(char const* text, int *is_present, int* is_signed, uint64_t* number)
{
    *is_present = 0;
    *number = 0;
    if (is_signed != NULL) {
        *is_signed = 0;
        if (*text == '-') {
            *is_signed = 1;
            text++;
            *is_present = 1;
        }
    }
    while (text[0] >= '0' && text[0] <= '9') {
        int delta = *text++ - '0';
        *number *= 10;
        *number += delta;
        *is_present = 1;
    }

    text = unibo_quicperf_parse_stream_spaces(text);

    return text;
}

char const* unibo_quicperf_skip_colon_if_present(char const* text, int is_present)
{
    if (is_present && text != NULL) {
        text = unibo_quicperf_parse_stream_spaces(text);
        if (*text == ':') {
            text++;
            text = unibo_quicperf_parse_stream_spaces(text);
        }
        else {
            text = NULL;
        }
    }
    return text;
}

char const* unibo_quicperf_parse_cnx_type(char const* text, unibo_quicperf_cnx_type_t* type)
{
    int is_present = 0;

    if (*text == 'd' || *text == 'D') {
        *type = unibo_quicperf_data_oriented;
        is_present = 1;
        text++;
    } 
    else if (*text == 't' || *text == 'T') {
        *type = unibo_quicperf_time_oriented;
        is_present = 1;
        text++;
    } 
    else {
        text = NULL;
    }

    return unibo_quicperf_skip_colon_if_present(text, is_present);
}

char const* unibo_quicperf_parse_stream_repeat(char const* text, uint64_t* number)
{
    int is_present = 0;

    if (*text == '*') {
        text = unibo_quicperf_parse_number(++text, &is_present, NULL, number);
        if (!is_present) {
            text = NULL;
        }
        else {
            text = unibo_quicperf_skip_colon_if_present(text, 1);
        }
    }
    else {
        *number = 1;
    }

    return text;
}

char const* unibo_quicperf_parse_stream_number(char const* text, uint64_t default_number, uint64_t* number)
{
    int is_present = 0;
    int is_signed = 0;

    text = unibo_quicperf_parse_number(text, &is_present, &is_signed, number);

    if (!is_present) {
        text = NULL;
    }
    else if (is_signed) {
        if (*number == 0) {
            *number = default_number;
        }
        else {
            text = NULL;
        }
    }

    return unibo_quicperf_skip_colon_if_present(text, is_present);
}

char const* unibo_quicperf_parse_post_size(char const* text, uint64_t default_number, uint64_t* number)
{
    int is_present = 0;
    int is_signed = 0;

    text = unibo_quicperf_parse_number(text, &is_present, &is_signed, number);

    if (!is_present) {
        text = NULL;
    }
    else if (is_signed) {
        if (*number == 0) {
            *number = default_number;
        }
        else {
            text = NULL;
        }
    }

    return unibo_quicperf_skip_colon_if_present(text, is_present);
}

char const* unibo_quicperf_parse_response_size(char const* text, int* is_signed, uint64_t* number)
{
    int is_present = 0;

    text = unibo_quicperf_parse_number(text, &is_present, is_signed, number);

    if (!is_present) {
        text = NULL;
    }

    return text;
}

char const* unibo_quicperf_parse_duration(char const* text, int* is_signed, uint64_t* duration) {
    int is_present = 0;

    text = unibo_quicperf_parse_number(text, &is_present, is_signed, duration);

    if (!is_present) {
        text = NULL;
    }

    if (strncmp(text, "s", 1) == 0) {
        *duration *= 1000000;
        text += 1;
    }
    else if (strncmp(text, "ms", 2) == 0) {
        *duration *= 1000;
        text += 2;
    }
    else if (strncmp(text, "us", 2) == 0) {
        text += 2;
    }
    else {
        text = NULL;
    }

    return text;
}

char const* unibo_quicperf_parse_stream_desc(char const* text, uint64_t default_stream, uint64_t default_previous,
    unibo_quicperf_stream_desc_t* desc)
{
    text = unibo_quicperf_parse_cnx_type(unibo_quicperf_parse_stream_spaces(text), &desc->type);

    if (text != NULL) {
        text = unibo_quicperf_parse_stream_repeat(text, &desc->repeat_count);
    }

    if (text != NULL) {
        text = unibo_quicperf_parse_stream_number(text, default_stream, &desc->stream_id);
    }

    if (text != NULL) {
        text = unibo_quicperf_parse_stream_number(text, default_previous, &desc->previous_stream_id);
    }

    if (desc->type == unibo_quicperf_data_oriented) {
        if (text != NULL) {
            text = unibo_quicperf_parse_post_size(unibo_quicperf_parse_stream_spaces(text), 0, &desc->post_size);
        }

        if (text != NULL) {
            text = unibo_quicperf_parse_response_size(unibo_quicperf_parse_stream_spaces(text),
                &desc->is_infinite, &desc->response_size);
        }
    }
    else if (desc->type == unibo_quicperf_time_oriented) {
        if (text != NULL) {
            text = unibo_quicperf_parse_duration(unibo_quicperf_parse_stream_spaces(text), 0, &desc->duration);
        }
    }

    /* Skip the final ';' */
    if (text != NULL) {
        if (*text == ';') {
            text++;
        }
        else if (*text != 0) {
            text = NULL;
        }
    }

    return text;
}

int unibo_quicperf_parse_scenario_desc(char const* text, size_t* nb_streams, unibo_quicperf_stream_desc_t** desc)
{
    int ret = 0;
    /* first count the number of streams and allocate memory */
    size_t nb_desc = unibo_quicperf_parse_nb_stream(text);
    size_t i = 0;
    uint64_t previous = UNIBO_QUICPERF_STREAM_ID_INITIAL;
    uint64_t stream_id = 0;

    *desc = (unibo_quicperf_stream_desc_t*)malloc(nb_desc * sizeof(unibo_quicperf_stream_desc_t));

    if (*desc == NULL) {
        *nb_streams = 0;
        ret = -1;
    }
    else {
        while (text != NULL) {
            text = unibo_quicperf_parse_stream_spaces(text);
            if (*text == 0) {
                break;
            }
            if (i >= nb_desc) {
                /* count was wrong! */
                break;
            }
            else {
                unibo_quicperf_stream_desc_t* stream_desc = &(*desc)[i];
                text = unibo_quicperf_parse_stream_desc(text, stream_id, previous, stream_desc);
                if (text != NULL) {
                    stream_id = stream_desc->stream_id + 4;
                    previous = stream_desc->stream_id;
                    i++;
                }
            }
        }

        *nb_streams = i;

        if (text == NULL) {
            ret = -1;
        }
    }

    return ret;
}

/* Management of spay of stream contexts
 */
 /* Stream splay management */

static int64_t unibo_quicperf_stream_ctx_compare(void* l, void* r)
{
    /* STream values are from 0 to 2^62-1, which means we are not worried with rollover */
    return ((unibo_quicperf_stream_ctx_t*)l)->stream_id - ((unibo_quicperf_stream_ctx_t*)r)->stream_id;
}

static picosplay_node_t* unibo_quicperf_stream_ctx_create(void* value)
{
    return &((unibo_quicperf_stream_ctx_t*)value)->unibo_quicperf_stream_node;
}


static void* unibo_quicperf_stream_ctx_value(picosplay_node_t* node)
{
    return (void*)((char*)node - offsetof(struct st_unibo_quicperf_stream_ctx, unibo_quicperf_stream_node));
}


static void unibo_quicperf_stream_ctx_delete(void* tree, picosplay_node_t* node)
{
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(tree);
#endif
    unibo_quicperf_stream_ctx_t* stream_ctx = unibo_quicperf_stream_ctx_value(node);

    free(stream_ctx);
}

/* Client work:
 * Integrate with context creation per client, on first reference.
 *
 * On start (ready), initialize the "initial" streams.
 * On fin of a stream, initialize the dependent stream.
 * In either of these cases, if nothing left, close the connection.
 *
 * On stream request: if stream is initialized, produce the required data, if all sent close stream. First 8 bytes
 * shall encode the number of bytes requested.
 * On stream data arrival: count the number arrived. If negative stream, reset stream if needed.
 *
 */

unibo_quicperf_stream_ctx_t* unibo_quicperf_find_stream_ctx(unibo_quicperf_ctx_t* ctx, uint64_t stream_id)
{
    unibo_quicperf_stream_ctx_t target;
    target.stream_id = stream_id;

    return (unibo_quicperf_stream_ctx_t*)picosplay_find(&ctx->unibo_quicperf_stream_tree, (void*)&target);
}

unibo_quicperf_ctx_t* unibo_quicperf_create_ctx(const char* scenario_text)
{
    unibo_quicperf_ctx_t* ctx = (unibo_quicperf_ctx_t*)malloc(sizeof(unibo_quicperf_ctx_t));

    if (ctx != NULL) {
        memset(ctx, 0, sizeof(unibo_quicperf_ctx_t));

        if (scenario_text != NULL) {
            if (unibo_quicperf_parse_scenario_desc(scenario_text, &ctx->nb_scenarios, &ctx->scenarios) == 0) {
                ctx->is_client = 1;
            }
            else {
                unibo_quicperf_delete_ctx(ctx);
                ctx = NULL;
            }
        }

        if (ctx != NULL) {
            picosplay_init_tree(&ctx->unibo_quicperf_stream_tree, unibo_quicperf_stream_ctx_compare, unibo_quicperf_stream_ctx_create,
                unibo_quicperf_stream_ctx_delete, unibo_quicperf_stream_ctx_value);
        }
    }

    return ctx;
}

void unibo_quicperf_delete_ctx(unibo_quicperf_ctx_t* ctx)
{
    picosplay_empty_tree(&ctx->unibo_quicperf_stream_tree);

    if (ctx->scenarios != NULL) {
        free(ctx->scenarios);
    }
    free(ctx);
}

unibo_quicperf_stream_ctx_t* unibo_quicperf_create_stream_ctx(unibo_quicperf_ctx_t* ctx, uint64_t stream_id)
{
    unibo_quicperf_stream_ctx_t* stream_ctx = (unibo_quicperf_stream_ctx_t*)malloc(sizeof(unibo_quicperf_stream_ctx_t));

    if (stream_ctx != NULL) {
        memset(stream_ctx, 0, sizeof(unibo_quicperf_stream_ctx_t));
        stream_ctx->stream_id = stream_id;
        picosplay_insert(&ctx->unibo_quicperf_stream_tree, stream_ctx);
    }
    return stream_ctx;
}

void unibo_quicperf_delete_stream_ctx(unibo_quicperf_ctx_t* ctx, unibo_quicperf_stream_ctx_t* stream_ctx)
{
    picosplay_delete_hint(&ctx->unibo_quicperf_stream_tree, &stream_ctx->unibo_quicperf_stream_node);
}

int unibo_quicperf_init_streams_from_scenario(picoquic_cnx_t* cnx, unibo_quicperf_ctx_t* ctx, uint64_t stream_id)
{
    int ret = 0;

    for (size_t i = 0; ret == 0 && i < ctx->nb_scenarios; i++) {
        if (ctx->scenarios[i].previous_stream_id == stream_id) {
            uint64_t repeat_nb = 0;
            uint64_t stream_x = ctx->scenarios[i].stream_id;
            do {
                unibo_quicperf_stream_ctx_t* stream_ctx = unibo_quicperf_create_stream_ctx(ctx, stream_x);
                if (ctx->scenarios[i].type == unibo_quicperf_data_oriented) {
                    stream_ctx->post_size = ctx->scenarios[i].post_size;
                    stream_ctx->response_size = ctx->scenarios[i].response_size;
                    stream_ctx->type = unibo_quicperf_data_oriented;
                }
                else if (ctx->scenarios[i].type == unibo_quicperf_time_oriented) {
                    stream_ctx->duration = ctx->scenarios[i].duration;
                    stream_ctx->type = unibo_quicperf_time_oriented;
                    ctx->scenarios[i].is_infinite = 1;
                }

                if (ctx->scenarios[i].is_infinite) {
                    stream_ctx->stop_for_fin = 1;
                    for (int x = 0; x < 8; x++) {
                        stream_ctx->length_header[x] = 0xFF;
                    }
                }
                else {
                    for (int x = 0; x < 8; x++) {
                        stream_ctx->length_header[x] = (uint8_t)((stream_ctx->response_size >> ((7 - x) * 8)) & 0xFF);
                    }
                }
                ret = picoquic_mark_active_stream(cnx, stream_ctx->stream_id, 1, stream_ctx);
                stream_x += 4;
                ctx->nb_open_streams++;
                repeat_nb++;
            } while (ret == 0 && repeat_nb < ctx->scenarios[i].repeat_count);
        }
    }

    return ret;
}

int unibo_quicperf_process_stream_data(picoquic_cnx_t * cnx, unibo_quicperf_ctx_t * ctx, unibo_quicperf_stream_ctx_t* stream_ctx,
    uint64_t stream_id, uint8_t* bytes, size_t length, picoquic_call_back_event_t fin_or_event)
{
    int ret = 0;

    /* Data arrival on stream #x, maybe with fin mark */

    if (stream_ctx == NULL && !ctx->is_client) {
        stream_ctx = unibo_quicperf_find_stream_ctx(ctx, stream_id);
        if (stream_ctx == NULL) {
            /* If this is the first appearance of a stream on the server side, create it */
            stream_ctx = unibo_quicperf_create_stream_ctx(ctx, stream_id);
        }
    }

    if (stream_ctx == NULL) {
        /* Hard error! */
        ret = -1;
    }
    else if (ctx->is_client) {
        if (!stream_ctx->is_closed) {
            stream_ctx->nb_response_bytes += length;
            ctx->data_received += length;

            if (stream_ctx->stop_for_fin) {
                if (stream_ctx->type == unibo_quicperf_data_oriented && stream_ctx->nb_response_bytes >= stream_ctx->response_size) {
                    if (!stream_ctx->is_stopped) {
                        /* ask to send sending. This will stop all data notifications for the stream. */
                        ret = picoquic_stop_sending(cnx, stream_ctx->stream_id, 0);
                        stream_ctx->is_stopped = 1;
                        stream_ctx->is_closed = 1;
                    }
                }
                else if (stream_ctx->type == unibo_quicperf_time_oriented && (ctx->last_interaction_time - picoquic_get_cnx_start_time(cnx) >= stream_ctx->duration)) {
                        ret = picoquic_stop_sending(cnx, stream_ctx->stream_id, 0);
                        stream_ctx->is_stopped = 1;
                        stream_ctx->is_closed = 1;
                }
                else if (fin_or_event == picoquic_callback_stream_fin && stream_ctx->type != unibo_quicperf_time_oriented) {
                    /* closed too soon! */
                    ret = picoquic_close(cnx, UNIBO_QUICPERF_ERROR_NOT_ENOUGH_DATA_SENT);
                }
            }
            else if (fin_or_event == picoquic_callback_stream_fin) {
                stream_ctx->is_closed = 1;
                if (stream_ctx->nb_response_bytes != stream_ctx->response_size) {
                    /* Error, server did not send the expected number of bytes */
                    ret = picoquic_close(cnx, UNIBO_QUICPERF_ERROR_NOT_ENOUGH_DATA_SENT);
                }
            }
            else if (stream_ctx->nb_response_bytes > stream_ctx->response_size) {
                /* error, too many bytes */
                ret = picoquic_close(cnx, UNIBO_QUICPERF_ERROR_TOO_MUCH_DATA_SENT);
            }

            if (stream_ctx->is_closed || fin_or_event == picoquic_callback_stream_fin) {
                ctx->nb_streams++;
            }

            if (stream_ctx->is_closed){
                ctx->nb_open_streams--;
                ret = unibo_quicperf_init_streams_from_scenario(cnx, ctx, stream_ctx->stream_id);
                if (ctx->nb_open_streams == 0) {
                    ret = picoquic_close(cnx, UNIBO_QUICPERF_NO_ERROR);
                } else if (ret == 0 && ctx->is_client) {
                    unibo_quicperf_delete_stream_ctx(ctx, stream_ctx);
                }
            }
        }
        else {
            /* Should never happen */
            ret = picoquic_close(cnx, UNIBO_QUICPERF_ERROR_TOO_MUCH_DATA_SENT);
        }
    }
    else if (!stream_ctx->is_closed) {
        /* Accumulate the first 8 bytes */
        size_t byte_index = 0;

        while (stream_ctx->nb_post_bytes < 8 && byte_index < length) {
            stream_ctx->response_size = (stream_ctx->response_size << 8) + bytes[byte_index++];
            stream_ctx->nb_post_bytes++;
        }
        stream_ctx->nb_post_bytes += (length - byte_index);

        if (fin_or_event == picoquic_callback_stream_fin) {
            if (stream_ctx->nb_post_bytes < 8) {
                stream_ctx->response_size = 0;
            }
            ret = picoquic_mark_active_stream(cnx, stream_ctx->stream_id, 1, stream_ctx);
        }
    }
    else {
        /* Should never happen */
        ret = picoquic_close(cnx, UNIBO_QUICPERF_ERROR_TOO_MUCH_DATA_SENT);
    }
    return ret;
}

int unibo_quicperf_prepare_to_send(picoquic_cnx_t* cnx, unibo_quicperf_ctx_t* ctx, unibo_quicperf_stream_ctx_t* stream_ctx,
    uint8_t* context, size_t length)
{

    int ret = 0;
    uint64_t send_limit = (ctx->is_client) ? stream_ctx->post_size : stream_ctx->response_size;
    uint64_t sent_already = (ctx->is_client) ? stream_ctx->nb_post_bytes : stream_ctx->nb_response_bytes;
    size_t available = length;
    int is_fin = 0;
    uint8_t* buffer;

    if (!ctx->is_client && stream_ctx->is_stopped) {
        available = 0;
        is_fin = 1;
    }
    else if (stream_ctx->type == unibo_quicperf_data_oriented && sent_already + available > send_limit) {
        is_fin = 1;
        available = (size_t)(send_limit - sent_already);
    }
    else if (stream_ctx->type == unibo_quicperf_time_oriented && (ctx->last_interaction_time - picoquic_get_cnx_start_time(cnx) >= stream_ctx->duration)) {
        is_fin = 1;
        available = 0;
    }
    if (ctx->is_client) {
        ctx->data_sent += available;
    }
    buffer = picoquic_provide_stream_data_buffer(context, available, is_fin, !is_fin);
    if (buffer != NULL) {
        while (ctx->is_client && stream_ctx->nb_post_bytes < 8 && available > 0) {
            *buffer++ = stream_ctx->length_header[stream_ctx->nb_post_bytes++];
            available--;
        }
        memset(buffer, 0x30, available);
        if (ctx->is_client) {
            stream_ctx->nb_post_bytes += available;
        }
        else {
            stream_ctx->nb_response_bytes += available;
        }

        if (is_fin && !ctx->is_client) {
            unibo_quicperf_delete_stream_ctx(ctx, stream_ctx);
        }
    } else if (available > 0) {
        ret = picoquic_close(cnx, UNIBO_QUICPERF_ERROR_INTERNAL_ERROR);
    }

    return ret;
}

int unibo_quicperf_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx)
{
    int ret = 0;
    unibo_quicperf_ctx_t* ctx = (unibo_quicperf_ctx_t*)callback_ctx;
    unibo_quicperf_stream_ctx_t * stream_ctx = (unibo_quicperf_stream_ctx_t*)v_stream_ctx;

    if (callback_ctx == NULL || callback_ctx == picoquic_get_default_callback_context(picoquic_get_quic_ctx(cnx))) {
        /* This will happen at the first call to  server */
        ctx = unibo_quicperf_create_ctx(NULL);
        if (ctx == NULL) {
            /* cannot handle the connection */
            picoquic_close(cnx, UNIBO_QUICPERF_ERROR_INTERNAL_ERROR);
            return -1;
        }
        else {
            picoquic_set_callback(cnx, unibo_quicperf_callback, ctx);
        }
    }

    ctx->last_interaction_time = picoquic_get_quic_time(picoquic_get_quic_ctx(cnx));
    ctx->progress_observed = 1;

    switch (fin_or_event) {
    case picoquic_callback_stream_data:
    case picoquic_callback_stream_fin:
        ret = unibo_quicperf_process_stream_data(cnx, ctx, stream_ctx, stream_id, bytes, length, fin_or_event);
        break;
    case picoquic_callback_prepare_to_send:
        if (stream_ctx == NULL) {
            /* Unexpected */
            ret = -1;
        }
        else {
            ret = unibo_quicperf_prepare_to_send(cnx, ctx, stream_ctx, bytes, length);
        }
        break;
    case picoquic_callback_stream_reset: /* Server reset stream #x */
        picoquic_reset_stream(cnx, stream_id, 0);
        break;
    case picoquic_callback_stop_sending:
        if (stream_ctx == NULL) {
            stream_ctx = unibo_quicperf_find_stream_ctx(ctx, stream_id);
        }
        if (stream_ctx != NULL) {
            if (ctx->is_client) {
                /* Unexpected. Treat as protocol error */
                ret = -1;
            }
            else {
                stream_ctx->is_stopped = 1;
            }
        }
        break;
    case picoquic_callback_stateless_reset: /* Connection is unknown at peer */
    case picoquic_callback_close: /* Received connection close */
    case picoquic_callback_application_close: /* Received application close */
        if (!ctx->is_client) {
            unibo_quicperf_delete_ctx(ctx);
        }
        picoquic_set_callback(cnx, NULL, NULL);
        break;
    case picoquic_callback_version_negotiation: /* Not something we would want... */
        break;
    case picoquic_callback_stream_gap:
        /* TODO: Define what error. Stop sending? */
        break;
    case picoquic_callback_almost_ready:
    case picoquic_callback_ready:
        picoquic_cnx_set_pmtud_required(cnx, 1);
        if (ctx->is_client && ctx->unibo_quicperf_stream_tree.root == NULL) {
            ret = unibo_quicperf_init_streams_from_scenario(cnx, ctx, UINT64_MAX);
            if (ret != 0 || ctx->nb_open_streams == 0) {
                picoquic_close(cnx, UNIBO_QUICPERF_ERROR_INTERNAL_ERROR);
            }
        }
        break;
    case picoquic_callback_request_alpn_list:
        break;
    case picoquic_callback_set_alpn:
        break;
    default:
        /* unexpected */
        break;
    }

    /* that's it */
    return ret;
}
