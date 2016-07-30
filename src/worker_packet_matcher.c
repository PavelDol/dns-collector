#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "packet_frame.h"
#include "frame_queue.h"
#include "worker_packet_matcher.h"
#include "packet_hash.h"

struct dns_worker_packet_matcher *
dns_worker_packet_matcher_create(dns_us_time_t matching_duration, struct dns_frame_queue *in, struct dns_frame_queue *out)
{
    assert(in);
    struct dns_worker_packet_matcher *pm = xmalloc_zero(sizeof(struct dns_worker_packet_matcher));
    pm->in = in;
    pm->out = out;
    pm->inframe = NULL;
    pm->outframe = NULL;
    pm->current_time = DNS_NO_TIME;
    pm->matching_duration = matching_duration;
    pm->frame_max_duration = 1.0; // TODO: copy from input or config
    pm->frame_max_size = 1024 * 1024; // TODO: copy from input or config
    assert(pm->matching_duration > 0);
    pthread_mutex_init(&pm->running, NULL);
    // Actually uses a random seed (indicated by the 0 value)
    pm->hash_table = dns_packet_hash_create(WORKER_PACKET_MATCHER_MIN_HASH_SIZE, 0);
    clist_init(pm->packet_queue);
    return pm;
}

void
dns_worker_packet_matcher_destroy(struct dns_worker_packet_matcher *pm)
{
    if (pthread_mutex_trylock(&pm->running) != 0)
        die("destroying a running logger");
    pthread_mutex_unlock(&pm->running);
    pthread_mutex_destroy(&pm->running);
    dns_packet_hash_destroy(pm->hash_table);
    free(pm);
}


/**
 * Outputs the current frame and creates a new one.
 */
static void
dns_worker_packet_matcher_output_frame(struct dns_worker_packet_matcher *pm)
{
    struct dns_packet_frame *new_frame = dns_packet_frame_create(pm->outframe->time_end, pm->outframe->time_end);
    dns_frame_queue_enqueue(pm->out, pm->outframe); // Hand over ownership
    pm->outframe = new_frame;
}


/**
 * Advance the time of the matcher to the given time.
 * Outputs any packets that should leave the matcher berore that time
 * and also any (empty) intermediate frames.
 * Note: pm->outframe must exit and its times and pm->current_time must not be DNS_NO_TIME
 */
static void
dns_worker_packet_matcher_advance_time_to(struct dns_worker_packet_matcher *pm, dns_us_time_t time)
{
    assert(pm && time != DNS_NO_TIME && pm->current_time != DNS_NO_TIME);
    if (time < pm->current_time) {
        msg(L_WARN, "Packet arrived at matcher %f s before the latest seen",
            dns_us_time_to_fsec(pm->current_time - time));
        return;
    }

    while (pm->current_time < time) {
        // What happens first - packet exit or frame end?
        struct dns_packet *pkt = clist_head(pm->packet_queue);
        dns_us_time_t ev_time = pm->outframe->time_start + pm->frame_max_duration;
        if (pkt)
            ev_time = MIN(ev_time, pkt->ts + pm->matching_duration);
        if (ev_time > time) {
            pm->current_time = time;
        } else {
            if (ev_time == pm->outframe->time_start + pm->frame_max_duration) {
                // Output frame before it is too long
                assert(pm->outframe->time_end <= ev_time);
                pm->outframe->time_end = ev_time;
                pm->current_time = ev_time;
                dns_worker_packet_matcher_output_frame(pm);
            } else {
                // Output the packet
                if (pm->outframe->size + pkt->memory_size > pm->frame_max_size)
                    dns_worker_packet_matcher_output_frame(pm);
                // Remove packet from queue and hashtable
                clist_unlink(pkt->node);
                // TODO; remove pkt from the hashtable if request
                dns_packet_frame_append_packet(pm->outframe, pkt);
                pm->current_time = ev_time;
            }
        }
    }
}


/** 
 * Returns the next packet to be processed, or NULL when a filal frame is seen.
 * Initializes the output frame after it sees the first frame.
 * Outputs any intermediate empty frames while seeking in input time.
 */
static struct dns_packet *
dns_worker_packet_matcher_next_packet(dns_worker_packet_matcher *pm)
{
    while(1) {
        // Load the first or next frame if not loaded
        if (!pm->inframe) {
            pm->inframe = dns_frame_queue_dequeue(pm->in);
            if (pm->inframe->type == 1) {
                dns_packet_frame_destroy(pm->inframe);
                pm->inframe = NULL;
                return NULL; // No more input packets, ever
            }
            assert(pm->inframe->time_start != DNS_NO_TIME);
            if (!pm->outframe) { // In case thid was the first frame
                pm->outframe = dns_packet_frame_create(pm->inframe->time_start, pm->inframe->time_start);
                pm->current_time = pm->inframe->time_start;
            }
            dns_worker_packet_matcher_advance_time_to(pm, pm->inframe->time_start);
        }

        if (clist_empty(pm->inframe->packets)) {
            dns_worker_packet_matcher_advance_time_to(pm, pm->inframe->time_end);
            dns_packet_frame_destroy(pm->inframe);
            pm->inframe = NULL;
        } else {
            struct dns_packet *pkt = clist_remove_head(pm->inframe->packets);
            dns_worker_packet_matcher_advance_time_to(pm, pkt->ts);
            return pkt;
        }
    }
}

static void*
dns_worker_packet_matcher_main(void *matcher)
{
    struct dns_worker_packet_matcher *pm = matcher;
    struct dns_packet *pkt;
    while((pkt = dns_worker_packet_matcher_next_packet(pm))) {
        // pm->curtime is already advanced. Insert and match the packet.
        clist_add_tail(pm->packet_queue, pkt->node); 
        // TODO: look up in hash, match (if response) or insert (if request)
    }
    dns_worker_packet_matcher_advance_time_to(pm, pm->curtime + pm->matching_duration + 1);
    if (pm->outframe) {
        dns_frame_queue_enqueue(pm->out, pm->outframe);
        pm->outframe = NULL;
    }
    dns_frame_queue_enqueue(pm->out, dns_packet_frame_create_final(pm->curtime));
    pthread_mutex_unlock(&l->running);
    return NULL;
}

void
dns_worker_packet_matcher_finish(struct dns_worker_packet_matcher *pm)
{
    int r = pthread_join(pm->thread, NULL);
    assert(r == 0);
    msg(L_DEBUG, "Worker packet matcher stopped and joined");
}

void
dns_worker_packet_matcher_start(struct dns_worker_packet_matcher *pm)
{
    if (pthread_mutex_trylock(&pm->running) != 0)
        die("starting a running worker");
    int r = pthread_create(&pm->thread, NULL, dns_worker_packet_matcher_main, pm);
    assert(r == 0);
    msg(L_DEBUG, "Worker packet matcher started (matching window %f s)", dns_us_time_to_fsec(pm->matching_duration));
}
