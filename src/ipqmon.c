/* ipqmon.c
 * Copyright (C) 2006 Tillmann Werner <tillmann.werner@gmx.de>
 *
 * This file is free software; as a special exception the author gives
 * unlimited permission to copy and/or distribute it, with or without
 * modifications, as long as this notice is preserved.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "honeytrap.h"
#ifdef USE_IPQ_MON

#include <errno.h>
#include <stdlib.h>
#include <linux/netfilter.h>
#include <libipq.h>
#include <errno.h>
#include <string.h>

#include "logging.h"
#include "dynsrv.h"
#include "ctrl.h"
#include "ipqmon.h"

// BUFSIZE >= 256 seem to work for new tcp connections
#define BUFSIZE 256

static void die(struct ipq_handle *h) {
ipq_perror("IPQ Error: ");
	logmsg(LOG_ERR, 1, "IPQ Error: %s - %s.\n", ipq_errstr(), strerror(errno));
	ipq_destroy_handle(h);
	clean_exit(0);
}

int start_ipq_mon(void) {
	int status, process;
	uint16_t sport, dport;
	unsigned char buf[BUFSIZE];
	ipq_packet_msg_t *packet;
	struct ipq_handle *h;
	struct ip_header *ip;
	struct tcp_header *tcp;
	struct udp_header *udp;

	sport	= 0;
	dport	= 0;
	packet	= NULL;
	ip	= NULL;
	tcp	= NULL;
	udp	= NULL;

	logmsg(LOG_DEBUG, 1, "Creating ipq connection monitor.\n");
	if ((h = ipq_create_handle(0, PF_INET)) == NULL) {
		die(h);
		logmsg(LOG_ERR, 1, "Error - Could not create IPQ handle.\n");
	}

	if ((status = ipq_set_mode(h, IPQ_COPY_PACKET, BUFSIZE)) < 0) {
		logmsg(LOG_ERR, 1, "Error - Could not set IPQ mode.\n");
		die(h);
	}

	logmsg(LOG_NOTICE, 1, "---- Trapping attacks via IPQ. ----\n");

	for (;;) {
		process	= 1;
		if ((status = ipq_read(h, buf, BUFSIZE, 0)) < 0) {
			logmsg(LOG_ERR, 1, "Error - Could not set verdict on packet.\n");
			die(h);
		}
		switch (ipq_message_type(buf)) {
			case NLMSG_ERROR:
				logmsg(LOG_ERR, 1, "IPQ Error - Error message received: %s\n",
					strerror(ipq_get_msgerr(buf)));
				break;
			case IPQM_PACKET:
				packet	= ipq_get_packet(buf);
				ip	= (struct ip_header*) packet->payload;
				if (ip->ip_p == TCP) {
					tcp	= (struct tcp_header*) (packet->payload + (4 * ip->ip_hlen));
					sport	= ntohs(tcp->th_sport);
					dport	= ntohs(tcp->th_dport);
				} else if (ip->ip_p == UDP) {
					udp	= (struct udp_header*) (packet->payload + (4 * ip->ip_hlen));
					sport	= ntohs(udp->uh_sport);
					dport	= ntohs(udp->uh_dport);
				}

				/* Got a connection request, fork handler and pass it back to the kernel */
				switch (port_flags[dport]) {
				case PORTCONF_NONE:
					logmsg(LOG_DEBUG, 1, "Port %u/%s has no explicit configuration.\n",
							dport, PROTO(ip->ip_p));
					break;
				case PORTCONF_IGNORE:
					logmsg(LOG_DEBUG, 1, "Port %u/%s is configured to be ignored.\n",
						dport, PROTO(ip->ip_p));
					if ((status = ipq_set_verdict(h, packet->packet_id, NF_ACCEPT, 0, NULL)) < 0) {
						logmsg(LOG_ERR, 1, "Error - Could not set verdict on packet.\n");
						die(h);
					}
					process = 0;
					break;
				case PORTCONF_NORMAL:
					logmsg(LOG_DEBUG, 1, "Port %u/%s is configured to be handled in normal mode.\n",
						dport, PROTO(ip->ip_p));
					break;
				case PORTCONF_MIRROR:
					logmsg(LOG_DEBUG, 1, "Port %u/%s is configured to be handled in mirror mode.\n",
						dport, PROTO(ip->ip_p));
					break;
				case PORTCONF_PROXY:
					logmsg(LOG_DEBUG, 1, "Port %u/%s is configured to be handled in proxy mode\n",
						dport, PROTO(ip->ip_p));
					break;
				default:
					logmsg(LOG_ERR, 1, "Error - Invalid explicit configuration for port %u/%s.\n",
						dport, PROTO(ip->ip_p));
					if ((status = ipq_set_verdict(h, packet->packet_id, NF_ACCEPT, 0, NULL)) < 0) {
						logmsg(LOG_ERR, 1, "Error - Could not set verdict on packet.\n");
						die(h);
					}
					process = 0;
					break;
				}
				
				if (process == 0) break;

				logmsg(LOG_INFO, 1, "Connection request on port %d/%s.\n", dport, PROTO(ip->ip_p));
				start_dynamic_server(ip->ip_src, htons(sport), ip->ip_dst, htons(dport), ip->ip_p);
				sleep(1);	//dirty, but you don't want IPC as alternative
				if ((status = ipq_set_verdict(h, packet->packet_id, NF_ACCEPT, 0, NULL)) < 0) {
					logmsg(LOG_ERR, 1, "Error - Could not set verdict on packet.\n");
					die(h);
				}
				break;
			default:
				logmsg(LOG_DEBUG, 1, "IPQ Warning - Unknown message type.\n");
				break;
		}
	}

	ipq_destroy_handle(h);
	return(1);
	}

#endif
