/* Copyright (c) 2011, TrafficLab, Ericsson Research, Hungary
 * Copyright (c) 2012, CPqD, Brazil 
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Ericsson Research nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *
 */

#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#include "include/openflow/openflow.h"
#include "oxm-match.h"
#include "ofl.h"
#include "ofl-actions.h"
#include "ofl-structs.h"
#include "ofl-utils.h"
#include "ofl-log.h"
#include "ofl-packets.h"


#define LOG_MODULE ofl_str_p
OFL_LOG_INIT(LOG_MODULE)


size_t
ofl_structs_instructions_ofp_len(struct ofl_instruction_header *instruction, struct ofl_exp *exp) {
    switch (instruction->type) {
        case OFPIT_GOTO_TABLE: {
            return sizeof(struct ofp_instruction_goto_table);
        }
        case OFPIT_WRITE_METADATA: {
            return sizeof(struct ofp_instruction_write_metadata);
        }
        case OFPIT_WRITE_ACTIONS:
        case OFPIT_APPLY_ACTIONS: {
            struct ofl_instruction_actions *i = (struct ofl_instruction_actions *)instruction;

            return sizeof(struct ofp_instruction_actions)
                   + ofl_actions_ofp_total_len(i->actions, i->actions_num, exp);
        }
        case OFPIT_CLEAR_ACTIONS: {
            return sizeof(struct ofp_instruction_actions);
        }
        case OFPIT_EXPERIMENTER: {
            if (exp == NULL || exp->inst == NULL || exp->inst->ofp_len == NULL) {
                OFL_LOG_WARN(LOG_MODULE, "Trying to len experimenter instruction, but no callback was given.");
                return -1;
            }
            return exp->inst->ofp_len(instruction);
        }
        default:
            OFL_LOG_WARN(LOG_MODULE, "Trying to len unknown instruction type.");
            return 0;
    }
}

size_t
ofl_structs_instructions_ofp_total_len(struct ofl_instruction_header **instructions, size_t instructions_num, struct ofl_exp *exp) {
    size_t sum;
    OFL_UTILS_SUM_ARR_FUN2(sum, instructions, instructions_num,
            ofl_structs_instructions_ofp_len, exp);
    return sum;
}

size_t
ofl_structs_instructions_pack(struct ofl_instruction_header *src, struct ofp_instruction *dst, struct ofl_exp *exp) {
    
    dst->type = htons(src->type);
    memset(dst->pad, 0x00, 4);
    
    switch (src->type) {
        case OFPIT_GOTO_TABLE: {
            struct ofl_instruction_goto_table *si = (struct ofl_instruction_goto_table *)src;
            struct ofp_instruction_goto_table *di = (struct ofp_instruction_goto_table *)dst;

            di->len = htons(sizeof(struct ofp_instruction_goto_table));
            di->table_id = si->table_id;
            memset(di->pad, 0x00, 3);

            return sizeof(struct ofp_instruction_goto_table);
        }
        case OFPIT_WRITE_METADATA: {
            struct ofl_instruction_write_metadata *si = (struct ofl_instruction_write_metadata *)src;
            struct ofp_instruction_write_metadata *di = (struct ofp_instruction_write_metadata *)dst;

            di->len = htons(sizeof(struct ofp_instruction_write_metadata));
            memset(di->pad, 0x00, 4);
            di->metadata = hton64(si->metadata);
            di->metadata_mask = hton64(si->metadata_mask);

            return sizeof(struct ofp_instruction_write_metadata);
        }
        case OFPIT_WRITE_ACTIONS:
        case OFPIT_APPLY_ACTIONS: {
            size_t total_len, len;
            uint8_t *data;
            size_t i;
            
            struct ofl_instruction_actions *si = (struct ofl_instruction_actions *)src;
            struct ofp_instruction_actions *di = (struct ofp_instruction_actions *)dst;

            total_len = sizeof(struct ofp_instruction_actions) + ofl_actions_ofp_total_len(si->actions, si->actions_num, exp);

            di->len = htons(total_len);
            memset(di->pad, 0x00, 4);
            data = (uint8_t *)dst + sizeof(struct ofp_instruction_actions);
            
            for (i=0; i<si->actions_num; i++) {
                len = ofl_actions_pack(si->actions[i], (struct ofp_action_header *)data, data, exp);
                data += len;
            }
            return total_len;
        }
        case OFPIT_CLEAR_ACTIONS: {
            size_t total_len;

            struct ofp_instruction_actions *di = (struct ofp_instruction_actions *)dst;

            total_len = sizeof(struct ofp_instruction_actions);

            di->len = htons(total_len);
            memset(di->pad, 0x00, 4);

            return total_len;
        }
        case OFPIT_EXPERIMENTER: {
            if (exp == NULL || exp->inst == NULL || exp->inst->pack == NULL) {
                OFL_LOG_WARN(LOG_MODULE, "Trying to pack experimenter instruction, but no callback was given.");
                return -1;
            }
            return exp->inst->pack((struct ofl_instruction_header *)src, dst);
        }
        default:
            OFL_LOG_WARN(LOG_MODULE, "Trying to pack unknown instruction type.");
            return 0;
    }
}



size_t
ofl_structs_buckets_ofp_len(struct ofl_bucket *bucket, struct ofl_exp *exp) {
    size_t total_len, rem;

    total_len = sizeof(struct ofp_bucket) + ofl_actions_ofp_total_len(bucket->actions, bucket->actions_num, exp);
    /* Note: buckets are 64 bit aligned according to spec 1.1 */
    rem = total_len % 8;
    return total_len + (rem == 0 ? 0 : (8 - rem));
}



size_t
ofl_structs_buckets_ofp_total_len(struct ofl_bucket **buckets, size_t buckets_num, struct ofl_exp *exp) {
    size_t sum;
    OFL_UTILS_SUM_ARR_FUN2(sum, buckets, buckets_num,
            ofl_structs_buckets_ofp_len, exp);
    return sum;
}


size_t
ofl_structs_bucket_pack(struct ofl_bucket *src, struct ofp_bucket *dst, struct ofl_exp *exp) {
    size_t total_len, rem, align, len;
    uint8_t *data;
    size_t i;

    total_len = sizeof(struct ofp_bucket) + ofl_actions_ofp_total_len(src->actions, src->actions_num, exp);
    /* Note: buckets are 64 bit aligned according to spec 1.1 draft 3 */
    rem = total_len % 8;
    align = rem == 0 ? 0 : (8-rem);
    total_len += align;

    dst->len = htons(total_len);
    dst->weight = htons(src->weight);
    dst->watch_port = htonl(src->watch_port);
    dst->watch_group = htonl(src->watch_group);
    memset(dst->pad, 0x00, 4);

    data = (uint8_t *)dst + sizeof(struct ofp_bucket);

    for (i=0; i<src->actions_num; i++) {
        len = ofl_actions_pack(src->actions[i], (struct ofp_action_header *)data, data, exp);
        data += len;
    }

    memset(data, 0x00, align);

    return total_len;
}


size_t
ofl_structs_flow_stats_ofp_len(struct ofl_flow_stats *stats, struct ofl_exp *exp) {
    
    return ROUND_UP((sizeof(struct ofp_flow_stats) - 4) + stats->match->length,8) +
           ofl_structs_instructions_ofp_total_len(stats->instructions, stats->instructions_num, exp);
}

size_t
ofl_structs_flow_stats_ofp_total_len(struct ofl_flow_stats ** stats, size_t stats_num, struct ofl_exp *exp) {
    size_t sum;
    OFL_UTILS_SUM_ARR_FUN2(sum, stats, stats_num,
            ofl_structs_flow_stats_ofp_len, exp);
    return sum;
}

size_t
ofl_structs_flow_stats_pack(struct ofl_flow_stats *src, uint8_t *dst, struct ofl_exp *exp) {
    
    struct ofp_flow_stats *flow_stats;
    size_t total_len;
    uint8_t *data;
    size_t  i;
    
    total_len = ROUND_UP(sizeof(struct ofp_flow_stats) -4 + src->match->length,8) +
                ofl_structs_instructions_ofp_total_len(src->instructions, src->instructions_num, exp);
    
    flow_stats = (struct ofp_flow_stats*) dst;

    flow_stats->length = htons(total_len);
    flow_stats->table_id = src->table_id;
    flow_stats->pad = 0x00;
    flow_stats->duration_sec = htonl(src->duration_sec);
    flow_stats->duration_nsec = htonl(src->duration_nsec);
    flow_stats->priority = htons(src->priority);
    flow_stats->idle_timeout = htons(src->idle_timeout);
    flow_stats->hard_timeout = htons(src->hard_timeout);
    memset(flow_stats->pad2, 0x00, 6);
    flow_stats->cookie = hton64(src->cookie);
    flow_stats->packet_count = hton64(src->packet_count);
    flow_stats->byte_count = hton64(src->byte_count);
    data = (dst) + sizeof(struct ofp_flow_stats) - 4;
    
    ofl_structs_match_pack(src->match, &(flow_stats->match), data, HOST_ORDER, exp);
    data = (dst) + ROUND_UP(sizeof(struct ofp_flow_stats) -4 + src->match->length, 8);  
    
    for (i=0; i < src->instructions_num; i++) {
        data += ofl_structs_instructions_pack(src->instructions[i], (struct ofp_instruction *) data, exp);
    }
    return total_len;
}

size_t
ofl_structs_group_stats_ofp_len(struct ofl_group_stats *stats) {
    return sizeof(struct ofp_group_stats) +
           sizeof(struct ofp_bucket_counter) * stats->counters_num;
}

size_t
ofl_structs_group_stats_ofp_total_len(struct ofl_group_stats ** stats, size_t stats_num) {
    size_t sum;
    OFL_UTILS_SUM_ARR_FUN(sum, stats, stats_num,
            ofl_structs_group_stats_ofp_len);
    return sum;
}

size_t
ofl_structs_group_stats_pack(struct ofl_group_stats *src, struct ofp_group_stats *dst) {
    size_t total_len, len;
    uint8_t *data;
    size_t i;

    total_len = sizeof(struct ofp_group_stats) +
                sizeof(struct ofp_bucket_counter) * src->counters_num;

    dst->length =       htons( total_len);
    memset(dst->pad, 0x00, 2);
    dst->group_id =     htonl( src->group_id);
    dst->ref_count =    htonl( src->ref_count);
    memset(dst->pad2, 0x00, 4);
    dst->packet_count = hton64(src->packet_count);
    dst->byte_count =   hton64(src->byte_count);

    data = (uint8_t *)dst->bucket_stats;

    for (i=0; i<src->counters_num; i++) {
        len = ofl_structs_bucket_counter_pack(src->counters[i], (struct ofp_bucket_counter *)data);
        data += len;
    }

    return total_len;
}

size_t
ofl_structs_group_desc_stats_ofp_len(struct ofl_group_desc_stats *stats, struct ofl_exp *exp) {
    return sizeof(struct ofp_group_desc_stats) +
           ofl_structs_buckets_ofp_total_len(stats->buckets, stats->buckets_num, exp);
}

size_t
ofl_structs_group_desc_stats_ofp_total_len(struct ofl_group_desc_stats ** stats, size_t stats_num, struct ofl_exp *exp) {
    size_t sum;
    OFL_UTILS_SUM_ARR_FUN2(sum, stats, stats_num,
            ofl_structs_group_desc_stats_ofp_len, exp);
    return sum;
}

size_t
ofl_structs_group_desc_stats_pack(struct ofl_group_desc_stats *src, struct ofp_group_desc_stats *dst, struct ofl_exp *exp) {
    size_t total_len, len;
    uint8_t *data;
    size_t i;

    total_len = sizeof(struct ofp_group_desc_stats) +
            ofl_structs_buckets_ofp_total_len(src->buckets, src->buckets_num, exp);

    dst->length =       htons( total_len);
    dst->type =                src->type;
    dst->pad = 0x00;
    dst->group_id =     htonl( src->group_id);

    data = (uint8_t *)dst->buckets;

    for (i=0; i<src->buckets_num; i++) {
        len = ofl_structs_bucket_pack(src->buckets[i], (struct ofp_bucket *)data, exp);
        data += len;
    }

    return total_len;
}


size_t
ofl_structs_queue_prop_ofp_total_len(struct ofl_queue_prop_header ** props,
                                     size_t props_num) {
    size_t sum;
    OFL_UTILS_SUM_ARR_FUN(sum, props, props_num,
            ofl_structs_queue_prop_ofp_len);
    return sum;
}

size_t
ofl_structs_queue_prop_ofp_len(struct ofl_queue_prop_header *prop) {
    switch (prop->type) {
       
        case OFPQT_MIN_RATE: {
            return sizeof(struct ofp_queue_prop_min_rate);
        }
        case OFPQT_MAX_RATE:{
           return sizeof(struct ofp_queue_prop_max_rate);
        }
        case OFPQT_EXPERIMENTER:{
           return sizeof(struct ofp_queue_prop_experimenter);
        }
    }
    return 0;
}

size_t
ofl_structs_queue_prop_pack(struct ofl_queue_prop_header *src,
                            struct ofp_queue_prop_header *dst) {
    dst->property = htons(src->type);
    memset(dst->pad, 0x00, 4);

    switch (src->type) {
       
        case OFPQT_MIN_RATE: {
            struct ofl_queue_prop_min_rate *sp = (struct ofl_queue_prop_min_rate *)src;
            struct ofp_queue_prop_min_rate *dp = (struct ofp_queue_prop_min_rate *)dst;

            dp->prop_header.len = htons(sizeof(struct ofp_queue_prop_min_rate));
            dp->rate            = htons(sp->rate);
            memset(dp->pad, 0x00, 6);

            return sizeof(struct ofp_queue_prop_min_rate);
        }
        case OFPQT_MAX_RATE:{
            struct ofl_queue_prop_max_rate *sp = (struct ofl_queue_prop_max_rate *)src;
            struct ofp_queue_prop_max_rate *dp = (struct ofp_queue_prop_max_rate *)dst;
            dp->prop_header.len = htons(sizeof(struct ofp_queue_prop_max_rate));
            dp->rate            = htons(sp->rate);
            memset(dp->pad, 0x00, 6);

            return sizeof(struct ofp_queue_prop_max_rate);
        }
        case OFPQT_EXPERIMENTER:{
            //struct ofl_queue_prop_experimenter *sp = (struct ofl_queue_prop_experimenter *)src;
            struct ofp_queue_prop_experimenter *dp = (struct ofp_queue_prop_experimenter*)dst;
            dp->prop_header.len = htons(sizeof(struct ofp_queue_prop_experimenter));
            memset(dp->pad, 0x00, 4);
            /*TODO Eder: How to copy without a know len?? */
            //dp->data = sp->data;
            return sizeof(struct ofp_queue_prop_experimenter);
        }
        default: {
            return 0;
        }
    }

}

size_t
ofl_structs_packet_queue_ofp_total_len(struct ofl_packet_queue ** queues,
                                       size_t queues_num) {
    size_t sum;
    OFL_UTILS_SUM_ARR_FUN(sum, queues, queues_num,
            ofl_structs_packet_queue_ofp_len);
    return sum;
}

size_t
ofl_structs_packet_queue_ofp_len(struct ofl_packet_queue *queue) {
    return sizeof(struct ofp_packet_queue) +
           ofl_structs_queue_prop_ofp_total_len(queue->properties,
                                                queue->properties_num);
}

size_t
ofl_structs_packet_queue_pack(struct ofl_packet_queue *src, struct ofp_packet_queue *dst) {
    size_t total_len, len;
    uint8_t *data;
    size_t i;

    total_len = sizeof(struct ofp_packet_queue) +
                ofl_structs_queue_prop_ofp_total_len(src->properties,
                                                     src->properties_num);

    dst->len = htons(total_len);
    memset(dst->pad, 0x00, 2);
    dst->queue_id = htonl(src->queue_id);

    data = (uint8_t *)dst + sizeof(struct ofp_packet_queue);

    for (i=0; i<src->properties_num; i++) {
        len = ofl_structs_queue_prop_pack(src->properties[i],
                                        (struct ofp_queue_prop_header *)data);
        data += len;
    }

    return total_len;
}


size_t
ofl_structs_port_pack(struct ofl_port *src, struct ofp_port *dst) {
    dst->port_no    = htonl(src->port_no);
    memset(dst->pad, 0x00, 4);
    memcpy(dst->hw_addr, src->hw_addr, ETH_ADDR_LEN);
    memset(dst->pad2, 0x00, 2);
    strncpy(dst->name, src->name, OFP_MAX_PORT_NAME_LEN);
    dst->config     = htonl(src->config);
    dst->state      = htonl(src->state);
    dst->curr       = htonl(src->curr);
    dst->advertised = htonl(src->advertised);
    dst->supported  = htonl(src->supported);
    dst->peer       = htonl(src->peer);
    dst->curr_speed = htonl(src->curr_speed);
    dst->max_speed  = htonl(src->max_speed);

    return sizeof(struct ofp_port);
}

size_t
ofl_structs_table_stats_pack(struct ofl_table_stats *src, struct ofp_table_stats *dst) {
    dst->table_id =    src->table_id;
    memset(dst->pad, 0x00, 3);
    dst->active_count =  htonl( src->active_count);
    dst->lookup_count =  hton64(src->lookup_count);
    dst->matched_count = hton64(src->matched_count);

    return sizeof(struct ofp_table_stats);
}

size_t
ofl_structs_port_stats_pack(struct ofl_port_stats *src, struct ofp_port_stats *dst) {
    dst->port_no      = htonl( src->port_no);
    memset(dst->pad, 0x00, 4);
    dst->rx_packets   = hton64(src->rx_packets);
    dst->tx_packets   = hton64(src->tx_packets);
    dst->rx_bytes     = hton64(src->rx_bytes);
    dst->tx_bytes     = hton64(src->tx_bytes);
    dst->rx_dropped   = hton64(src->rx_dropped);
    dst->tx_dropped   = hton64(src->tx_dropped);
    dst->rx_errors    = hton64(src->rx_errors);
    dst->tx_errors    = hton64(src->tx_errors);
    dst->rx_frame_err = hton64(src->rx_frame_err);
    dst->rx_over_err  = hton64(src->rx_over_err);
    dst->rx_crc_err   = hton64(src->rx_crc_err);
    dst->collisions   = hton64(src->collisions);

    return sizeof(struct ofp_port_stats);
}

size_t
ofl_structs_queue_stats_pack(struct ofl_queue_stats *src, struct ofp_queue_stats *dst) {
    dst->port_no = htonl(src->port_no);
    dst->queue_id = htonl(src->queue_id);
    dst->tx_bytes = hton64(src->tx_bytes);
    dst->tx_packets = hton64(src->tx_packets);
    dst->tx_errors = hton64(src->tx_errors);

    return sizeof(struct ofp_queue_stats);
}

size_t
ofl_structs_bucket_counter_pack(struct ofl_bucket_counter *src, struct ofp_bucket_counter *dst) {
    dst->packet_count = hton64(src->packet_count);
    dst->byte_count = hton64(src->byte_count);

    return sizeof(struct ofp_bucket_counter);
}


size_t
ofl_structs_match_ofp_len(struct ofl_match_header *match, struct ofl_exp *exp) {
    switch (match->type) {
        case (OFPMT_STANDARD): {
            return (sizeof(struct ofp_match));
        }
        default: {
            if (exp == NULL || exp->match == NULL || exp->match->ofp_len == NULL) {
                OFL_LOG_WARN(LOG_MODULE, "Trying to len experimenter match, but no callback was given.");
                return 0;
            }
            return exp->match->ofp_len(match);
        }
    }
}

size_t
ofl_structs_match_pack(struct ofl_match_header *src, struct ofp_match *dst, uint8_t* oxm_fields, enum byte_order order, struct ofl_exp *exp) {
    switch (src->type) {
        case (OFPMT_OXM): {
            struct ofl_match *m = (struct ofl_match *)src;
            struct ofpbuf *b = ofpbuf_new(0);
            int oxm_len;
            dst->type = htons(m->header.type);
            oxm_fields = (uint8_t*) &dst->oxm_fields;
            dst->length = htons(sizeof(struct ofp_match));
            if (src->length){
                if (order == HOST_ORDER)
                    oxm_len = oxm_put_match(b, m);
                else oxm_len = oxm_put_packet_match(b,m);
                memcpy(oxm_fields, (uint8_t*) ofpbuf_pull(b,oxm_len), oxm_len);
                dst->length = htons(oxm_len + ((sizeof(struct ofp_match )-4)));
                ofpbuf_delete(b);
                return ntohs(dst->length);
            }
            else return 0;
        }
        default: {
            if (exp == NULL || exp->match == NULL || exp->match->pack == NULL) {
                OFL_LOG_WARN(LOG_MODULE, "Trying to pack experimenter match, but no callback was given.");
                return -1;
            }
            return exp->match->pack(src, dst);
        }
    }
}

