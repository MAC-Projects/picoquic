// For now just a copy of quicperf.h

#ifndef UNIBO_QUICPERF_H
#define UNIBO_QUICPERF_H

#define UNIBO_QUICPERF_ALPN "unibo-perf"
#define UNIBO_QUICPERF_ALPN_LEN 10

#define UNIBO_QUICPERF_NO_ERROR 0
#define UNIBO_QUICPERF_ERROR_NOT_IMPLEMENTED 1
#define UNIBO_QUICPERF_ERROR_INTERNAL_ERROR 2
#define UNIBO_QUICPERF_ERROR_NOT_ENOUGH_DATA_SENT 3
#define UNIBO_QUICPERF_ERROR_TOO_MUCH_DATA_SENT 4

#define UNIBO_QUICPERF_STREAM_ID_INITIAL UINT64_MAX

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    unibo_quicperf_data_oriented = 0,
    unibo_quicperf_time_oriented
} unibo_quicperf_cnx_type_t;

typedef struct st_unibo_quicperf_stream_desc_t {
    unibo_quicperf_cnx_type_t type; /* either based on data or on time */
    uint64_t repeat_count;
    uint64_t stream_id; /* if -, use default  */
    uint64_t previous_stream_id; /* if -, use default  */
    uint64_t post_size; /* Mandatory */
    uint64_t response_size; /* If infinite, client will ask stop sending at this size */
    int is_infinite; /* Set if the response size was set to "-xxx" */
    uint64_t duration; /* duration of the connection in seconds */
} unibo_quicperf_stream_desc_t;

typedef struct st_unibo_quicperf_stream_ctx {
    picosplay_node_t unibo_quicperf_stream_node;
    uint64_t stream_id;
    uint8_t length_header[8];
    uint64_t post_size; /* Unknown on server, from scenario on client */
    uint64_t nb_post_bytes;  /* Sent on client, received on server */
    uint64_t response_size; /* From data on server, from scenario on client */
    uint64_t nb_response_bytes; /* Received on client, sent on server */
    uint64_t post_time; /* Time stream open (client) or first byte received (server) */
    uint64_t post_fin_time; /* Time last byte sent (client) or received (server) */
    uint64_t response_time; /* Time first byte sent (server) or received (client) */
    uint64_t response_fin_time; /* Time last byte sent (server) or received (client) */
    int stop_for_fin;
    int is_stopped;
    int is_closed;
    unibo_quicperf_cnx_type_t type; /* type of the connection*/
    uint64_t duration; /* duration of the connection in seconds */
} unibo_quicperf_stream_ctx_t;

typedef struct st_unibo_quicperf_ctx_t {
    int is_client;
    int progress_observed;
    size_t nb_scenarios;
    size_t nb_open_streams;
    uint64_t last_interaction_time;
    unibo_quicperf_stream_desc_t* scenarios;
    picosplay_tree_t unibo_quicperf_stream_tree;
    /* Statistics gathered on client */
    uint64_t data_sent;
    uint64_t data_received;
    uint64_t nb_streams;
} unibo_quicperf_ctx_t;

unibo_quicperf_ctx_t* unibo_quicperf_create_ctx(const char* scenario_text);
void unibo_quicperf_delete_ctx(unibo_quicperf_ctx_t* ctx);

int unibo_quicperf_callback(picoquic_cnx_t* cnx,
    uint64_t stream_id, uint8_t* bytes, size_t length,
    picoquic_call_back_event_t fin_or_event, void* callback_ctx, void* v_stream_ctx);

#ifdef __cplusplus
}
#endif

#endif /* UNIBO_QUICPERF_H */
