/*
* Author: Christian Huitema
* Copyright (c) 2017, Private Octopus, Inc.
* All rights reserved.
*
* Permission to use, copy, modify, and distribute this software for any
* purpose with or without fee is hereby granted, provided that the above
* copyright notice and this permission notice appear in all copies.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL Private Octopus, Inc. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "picoquic_internal.h"
#include <stdlib.h>
#include <string.h>
#include "cc_common.h"

// temp
#include <math.h>

typedef enum {
    picoquic_hybla_alg_slow_start = 0,
    picoquic_hybla_alg_congestion_avoidance
} picoquic_hybla_alg_state_t;

typedef struct st_picoquic_hybla_state_t {
    picoquic_hybla_alg_state_t alg_state;
    uint64_t cwin;
    uint64_t residual_ack;
    uint64_t ssthresh;
    uint64_t recovery_start;
    uint64_t recovery_sequence;

    //uint64_t rtt0;
    //uint64_t rho;
    double increment_frac_sum;

    picoquic_min_max_rtt_t rtt_filter;
} picoquic_hybla_state_t;

static void picoquic_hybla_enter_recovery(
    picoquic_hybla_state_t* hybla_state,
    picoquic_cnx_t* cnx,
    picoquic_path_t * path_x,
    picoquic_congestion_notification_t notification,
    uint64_t current_time)
{
    hybla_state->ssthresh = hybla_state->cwin / 2;
    if (hybla_state->ssthresh < PICOQUIC_CWIN_MINIMUM) {
        hybla_state->ssthresh = PICOQUIC_CWIN_MINIMUM;
    }

    if (notification == picoquic_congestion_notification_timeout) {
        hybla_state->cwin = PICOQUIC_CWIN_MINIMUM;
        hybla_state->alg_state = picoquic_hybla_alg_slow_start;
    }
    else {
        hybla_state->cwin = hybla_state->ssthresh;
        hybla_state->alg_state = picoquic_hybla_alg_congestion_avoidance;
    }

    hybla_state->recovery_start = current_time;
    hybla_state->recovery_sequence = picoquic_cc_get_sequence_number(cnx, path_x);
    hybla_state->residual_ack = 0;
}

static void picoquic_hybla_seed_cwin(
    picoquic_hybla_state_t* hybla_state,
    picoquic_path_t* path_x,
    uint64_t bytes_in_flight) {

    if (hybla_state->alg_state == picoquic_hybla_alg_slow_start &&
        hybla_state->ssthresh == UINT64_MAX) {
        if (bytes_in_flight > hybla_state->cwin) {
            hybla_state->cwin = bytes_in_flight;
            hybla_state->ssthresh = bytes_in_flight;
            hybla_state->alg_state = picoquic_hybla_alg_congestion_avoidance;
        }
    }
}

static void picoquic_hybla_reset(picoquic_hybla_state_t* hybla_state, picoquic_path_t* path_x) {
    memset(hybla_state, 0, sizeof(picoquic_hybla_state_t));

    hybla_state->alg_state = picoquic_hybla_alg_slow_start;
    hybla_state->ssthresh = UINT64_MAX;
    hybla_state->cwin = PICOQUIC_CWIN_INITIAL;

    //hybla_state->rtt0 = 25;
    //hybla_state->rho = 0;

    path_x->cwin = hybla_state->cwin;
}

static void picoquic_hybla_init(picoquic_cnx_t * cnx, picoquic_path_t* path_x, uint64_t current_time) {
    
    /* Initialize the state of the congestion control algorithm */
    picoquic_hybla_state_t* hybla_state = (picoquic_hybla_state_t*)malloc(sizeof(picoquic_hybla_state_t));
#ifdef _WINDOWS
    UNREFERENCED_PARAMETER(current_time);
    UNREFERENCED_PARAMETER(cnx);
#endif

    if (hybla_state != NULL) {
        picoquic_hybla_reset(hybla_state, path_x);
        path_x->congestion_alg_state = hybla_state;
    }
    else {
        path_x->congestion_alg_state = NULL;
    }
}

static void picoquic_hybla_notify(
    picoquic_cnx_t * cnx,
    picoquic_path_t* path_x,
    picoquic_congestion_notification_t notification,
    picoquic_per_ack_state_t * ack_state,
    uint64_t current_time)
{

    picoquic_hybla_state_t* hybla_state = (picoquic_hybla_state_t*)path_x->congestion_alg_state;

    path_x->is_cc_data_updated = 1;

    if (hybla_state != NULL) {
        switch (notification) {
        case picoquic_congestion_notification_acknowledgement:
            if (hybla_state->alg_state == picoquic_hybla_alg_slow_start &&
                hybla_state->ssthresh == UINT64_MAX) {
                /* RTT measurements will happen before acknowledgement is signalled */
                uint64_t max_win = path_x->peak_bandwidth_estimate * path_x->smoothed_rtt / 1000000;
                uint64_t min_win = max_win /= 2;
                if (hybla_state->cwin < min_win) {
                    hybla_state->cwin = min_win;
                    path_x->cwin = min_win;
                }
            }

            if (path_x->last_time_acked_data_frame_sent > path_x->last_sender_limited_time) {
                switch (hybla_state->alg_state) {
                    case picoquic_hybla_alg_slow_start:
                        
                        // NEW
                        double rho = path_x->smoothed_rtt / path_x->rtt_min;
                        //double rho = path_x->smoothed_rtt / 25000000;
                        double increment_in_mss = pow(2.0, rho) - 1.0;
                        double increment = ack_state->nb_bytes_acknowledged * increment_in_mss;
                        int increment_int_part = floor(increment);
                        double increment_frac_part = increment - increment_int_part;
                                             
                        hybla_state->cwin += increment_int_part;
                    
                        hybla_state->increment_frac_sum += increment_frac_part;
                        
                        if (hybla_state->increment_frac_sum >= 1.0) {
                            hybla_state->cwin += 1;
                            hybla_state->increment_frac_sum -= 1.0;
                        }
                        
                        // OLD
                        //hybla_state->cwin += ack_state->nb_bytes_acknowledged;

                        /* if cnx->cwin exceeds SSTHRESH, exit and go to CA */
                        if (hybla_state->cwin >= hybla_state->ssthresh) {
                            hybla_state->alg_state = picoquic_hybla_alg_congestion_avoidance;
                        }
                        break;
                    case picoquic_hybla_alg_congestion_avoidance:
                    default: {

                        // NEW
                        double rho = path_x->smoothed_rtt / path_x->rtt_min;
                        //printf("%ld", path_x->rtt_min);
                        //double rho = path_x->smoothed_rtt / 25000000;
                        uint64_t complete_delta = ack_state->nb_bytes_acknowledged * path_x->send_mtu;
                        double increment = rho * rho * complete_delta / hybla_state->cwin;
                        int increment_int_part = floor(increment);
                        double increment_frac_part = increment - floor(increment);
                        
                        hybla_state->cwin += increment_int_part;

                        hybla_state->increment_frac_sum += increment_frac_part;
                        
                        if (hybla_state->increment_frac_sum >= 1.0) {
                            hybla_state->cwin += 1;
                            hybla_state->increment_frac_sum -= 1.0;
                        }
                        
                        // OLD
                        /*
                        uint64_t complete_delta = ack_state->nb_bytes_acknowledged * path_x->send_mtu + hybla_state->residual_ack;
                        hybla_state->residual_ack = complete_delta % hybla_state->cwin;
                        hybla_state->cwin += complete_delta / hybla_state->cwin;
                        */
                        break;
                    }
                }
                path_x->cwin = hybla_state->cwin;
            }
            break;
        case picoquic_congestion_notification_seed_cwin:
            picoquic_hybla_seed_cwin(hybla_state, path_x, ack_state->nb_bytes_acknowledged);
            break;
        case picoquic_congestion_notification_ecn_ec:
        case picoquic_congestion_notification_repeat:
        case picoquic_congestion_notification_timeout:
            /* if the loss happened in this period, enter recovery */
            if (hybla_state->recovery_sequence <= ack_state->lost_packet_number) {
                picoquic_hybla_enter_recovery(hybla_state, cnx, path_x, notification, current_time);
            }
            break;
        case picoquic_congestion_notification_spurious_repeat:
            if (!cnx->is_multipath_enabled) {
                if (current_time - hybla_state->recovery_start < path_x->smoothed_rtt &&
                    hybla_state->recovery_sequence > picoquic_cc_get_ack_number(cnx, path_x)) {
                    /* If spurious repeat of initial loss detected,
                    * exit recovery and reset threshold to pre-entry cwin.
                    */
                    if (hybla_state->ssthresh != UINT64_MAX &&
                        hybla_state->cwin < 2 * hybla_state->ssthresh) {
                        hybla_state->cwin = 2 * hybla_state->ssthresh;
                        hybla_state->alg_state = picoquic_hybla_alg_congestion_avoidance;
                    }
                }
            }
            else {
                if (current_time - hybla_state->recovery_start < path_x->smoothed_rtt &&
                    hybla_state->recovery_start > picoquic_cc_get_ack_sent_time(cnx, path_x)) {
                    /* If spurious repeat of initial loss detected,
                    * exit recovery and reset threshold to pre-entry cwin.
                    */
                    if (hybla_state->ssthresh != UINT64_MAX &&
                        hybla_state->cwin < 2 * hybla_state->ssthresh) {
                        hybla_state->cwin = 2 * hybla_state->ssthresh;
                        hybla_state->alg_state = picoquic_hybla_alg_congestion_avoidance;
                    }
                }
            }
            path_x->cwin = hybla_state->cwin;
            path_x->is_ssthresh_initialized = 1;
            break;
        case picoquic_congestion_notification_rtt_measurement:
            /* Using RTT increases as signal to get out of initial slow start */
            if (hybla_state->alg_state == picoquic_hybla_alg_slow_start &&
                hybla_state->ssthresh == UINT64_MAX){

                if (path_x->rtt_min > PICOQUIC_TARGET_RENO_RTT) {
                    uint64_t min_win;

                    if (path_x->rtt_min > PICOQUIC_TARGET_SATELLITE_RTT) {
                        min_win = (uint64_t)((double)PICOQUIC_CWIN_INITIAL * (double)PICOQUIC_TARGET_SATELLITE_RTT / (double)PICOQUIC_TARGET_RENO_RTT);
                    }
                    else {
                        /* Increase initial CWIN for long delay links. */
                        min_win = (uint64_t)((double)PICOQUIC_CWIN_INITIAL * (double)path_x->rtt_min / (double)PICOQUIC_TARGET_RENO_RTT);
                    }
                    if (min_win > hybla_state->cwin) {
                        hybla_state->cwin = min_win;
                        path_x->cwin = min_win;
                    }
                }

                if (picoquic_hystart_test(&hybla_state->rtt_filter, (cnx->is_time_stamp_enabled) ? ack_state->one_way_delay : ack_state->rtt_measurement,
                    cnx->path[0]->pacing.packet_time_microsec, current_time,
                    cnx->is_time_stamp_enabled)) {
                    /* RTT increased too much, get out of slow start! */
                    hybla_state->ssthresh = hybla_state->cwin;
                    hybla_state->alg_state = picoquic_hybla_alg_congestion_avoidance;
                    path_x->cwin = hybla_state->cwin;
                    path_x->is_ssthresh_initialized = 1;
                }
            }
            break;
        case picoquic_congestion_notification_cwin_blocked:
            break;
        case picoquic_congestion_notification_reset:
            picoquic_hybla_reset(hybla_state, path_x);
            break;
        default:
            /* ignore */
            break;
        }

        /* Compute pacing data */
        picoquic_update_pacing_data(cnx, path_x, hybla_state->alg_state == picoquic_hybla_alg_slow_start &&
            hybla_state->ssthresh == UINT64_MAX);
    }
}

/* Release the state of the congestion control algorithm */
static void picoquic_hybla_delete(picoquic_path_t* path_x) {
    
    if (path_x->congestion_alg_state != NULL) {
        free(path_x->congestion_alg_state);
        path_x->congestion_alg_state = NULL;
    }
}

/* Observe the state of congestion control */

void picoquic_hybla_observe(picoquic_path_t* path_x, uint64_t* cc_state, uint64_t* cc_param) {

    picoquic_hybla_state_t* hybla_state = (picoquic_hybla_state_t*)path_x->congestion_alg_state;

    *cc_state = (uint64_t)hybla_state->alg_state;
    *cc_param = (hybla_state->ssthresh == UINT64_MAX) ? 0 : hybla_state->ssthresh;
}

/* Definition record for the Hybla algorithm */

#define PICOQUIC_HYBLA_ID "hybla" 

picoquic_congestion_algorithm_t picoquic_hybla_algorithm_struct = {
    PICOQUIC_HYBLA_ID, PICOQUIC_CC_ALGO_NUMBER_HYBLA,
    picoquic_hybla_init,
    picoquic_hybla_notify,
    picoquic_hybla_delete,
    picoquic_hybla_observe
};

picoquic_congestion_algorithm_t* picoquic_hybla_algorithm = &picoquic_hybla_algorithm_struct;
