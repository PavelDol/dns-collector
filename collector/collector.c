
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <pcap/pcap.h>

#include "collector.h"
#include "timeframe.h"
#include "packet.h"

dns_collector_t *
dns_collector_create(struct dns_config *conf)
{
    assert(conf);

    dns_collector_t *col = (dns_collector_t *)xmalloc_zero(sizeof(dns_collector_t));
  
    col->config = conf;
    CLIST_FOR_EACH(struct dns_output*, out, conf->outputs) {
        out->col = col;
    }
    
    return col;
}


void
collector_run(dns_collector_t *col)
{
    dns_ret_t r;

    for (char ** in = col->config->inputs; *in; in++) {
        r = dns_collector_open_pcap_file(col, *in);
        assert(r == DNS_RET_OK);

        while(r = dns_collector_next_packet(col), r == DNS_RET_OK) { }
        if (r == DNS_RET_ERR)
            break;
    }
}


void
dns_collector_destroy(dns_collector_t *col)
{ 
    assert(col && col->pcap);

    // Write remaining data
    if (col->tf_old)
        dns_timeframe_writeout(col->tf_old);

    if (col->tf_cur)
        dns_timeframe_writeout(col->tf_cur);
    
    // Flush and close outputs
    CLIST_FOR_EACH(struct dns_output*, out, col->config->outputs) {
        dns_output_close(out, col->tf_cur->time_end);
    }

    if (col->tf_old)
        dns_timeframe_destroy(col->tf_old);
    if (col->tf_cur)
        dns_timeframe_destroy(col->tf_cur);

    pcap_close(col->pcap);
   
    free(col);
}


dns_ret_t
dns_collector_open_pcap_file(dns_collector_t *col, const char *pcap_fname)
{
    char pcap_errbuf[PCAP_ERRBUF_SIZE];

    assert(col && col->pcap && pcap_fname);

    pcap_t *newcap = pcap_open_offline(pcap_fname, pcap_errbuf);

    if (!newcap) {
        msg(L_ERROR, "libpcap error: %s", pcap_errbuf);
        return DNS_RET_ERR;
    }

    if (pcap_datalink(newcap) != DLT_RAW) {
        msg(L_ERROR, "pcap with link %s not supported (only DLT_RAW)", pcap_datalink_val_to_name(pcap_datalink(newcap)));
        pcap_close(newcap);
        return DNS_RET_ERR;
    }

    pcap_close(col->pcap);
    col->pcap = newcap;
    return DNS_RET_OK;
}


dns_ret_t
dns_collector_next_packet(dns_collector_t *col)
{
    int r;
    struct pcap_pkthdr *pkt_header;
    const u_char *pkt_data;

    assert(col && col->pcap);

    r = pcap_next_ex(col->pcap, &pkt_header, &pkt_data);

    switch(r) {
        case -2: 
            return DNS_RET_EOF;

        case -1:
            msg(L_ERROR, "pcap: %s", pcap_geterr(col->pcap));
            return DNS_RET_ERR;

        case 0:
            return DNS_RET_TIMEOUT;

        case 1:
            col->stats.packets_captured++;

            dns_us_time_t now = dns_us_time_from_timeval(&(pkt_header->ts));
            if (!col->tf_cur) {
                dns_collector_rotate_frames(col, now);
                dns_collector_rotate_outputs(col, now);
            } else {
                // Possibly rotate several times to fill any gaps
                while (col->tf_cur->time_start + col->config->timeframe_length < now) {
                    dns_collector_rotate_frames(col, col->tf_cur->time_start + col->config->timeframe_length);
                    dns_collector_rotate_outputs(col, col->tf_cur->time_start + col->config->timeframe_length);
                }
            }

            dns_collector_process_packet(col, pkt_header, pkt_data);

            return DNS_RET_OK;
    }

    assert(0);
}

void
dns_collector_process_packet(dns_collector_t *col, struct pcap_pkthdr *pkt_header, const u_char *pkt_data)
{
    assert(col && pkt_header && pkt_data && col->tf_cur);

    dns_packet_t *pkt = dns_packet_create();
    assert(pkt);

    dns_packet_from_pcap(col, pkt, pkt_header, pkt_data);
    dns_ret_t r = dns_packet_parse(col, pkt);
    if (r == DNS_RET_DROPPED) {
        free(pkt);
        return;
    }

    // Matching request 
    dns_packet_t *req = NULL;

    if (DNS_PACKET_IS_RESPONSE(pkt)) {
        if (col->tf_old) {
            req = dns_timeframe_match_response(col->tf_old, pkt);
        }
        if (req == NULL) {
            req = dns_timeframe_match_response(col->tf_cur, pkt);
        }
    }

    if (req)
        return; // packet given to a matching request
        
    dns_timeframe_append_packet(col->tf_cur, pkt);
}

void
dns_collector_rotate_outputs(dns_collector_t *col, dns_us_time_t time_now)
{
    CLIST_FOR_EACH(struct dns_output*, out, col->config->outputs) {
        dns_output_check_rotation(out, time_now);
    }
}

void
dns_collector_rotate_frames(dns_collector_t *col, dns_us_time_t time_now)
{
    if (col->tf_old) {
        dns_timeframe_writeout(col->tf_old);
        dns_timeframe_destroy(col->tf_old);
        col->tf_old = NULL;
    }

    if (time_now == DNS_NO_TIME) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        time_now = dns_us_time_from_timespec(&now);
    }

    if (col->tf_cur) {
        col->tf_cur -> time_end = time_now - 1; // prevent overlaps
        col->tf_old = col->tf_cur;
        col->tf_cur = NULL;
    }

    col->tf_cur = dns_timeframe_create(col, time_now);
}
