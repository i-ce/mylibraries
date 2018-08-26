#include "ethernet.h"
#include <string.h>
#include <stdio.h>

#define be16toword(a) ((((a)>>8)&0xff)|(((a)<<8)&0xff00))
#define ETH_ARP be16toword(0x0806)
#define ETH_IP be16toword(0x0800)
#define ARP_ETH	be16toword(0x0001)
#define ARP_IP	be16toword(0x0800)
#define ARP_REQUEST	be16toword(1)
#define ARP_REPLY	be16toword(2)
#define IP_ICMP			1
#define IP_TCP			6
#define IP_UDP			17
#define ICMP_REQ		8
#define ICMP_REPLY	0
#define TCP_FIN			1
#define TCP_SYN			2
#define TCP_ACK			16

/* Local driver */
ethernet_t * ethernet;
uint16_t id = 0;

/* Local functions */
static void eth_send(eth_frame_t *frame, uint16_t len);
static void eth_read(eth_frame_t *frame, uint16_t len);
static uint8_t arp_read(eth_frame_t *frame, uint16_t len);
static void arp_send(eth_frame_t *frame);
static uint8_t ip_send(eth_frame_t *frame, uint16_t len);
static uint8_t ip_read(eth_frame_t *frame, uint16_t len);
static uint8_t icmp_read(eth_frame_t *frame, uint16_t len);
static uint16_t checksum(uint8_t *ptr, uint16_t len);
static udp_packet_t * udp_receive (uint8_t * buf, uint16_t len);
static tcp_packet_t * tcp_receive (uint8_t * buf, uint16_t len);

/* Public fuctions */
ethernet_t * ethernet_Init (ethernet_t * this)
{
	if (this) ethernet = this;
	
	return ethernet;
}

void net_poll(void)
{
	uint16_t len;
	eth_frame_t *frame = (void*)ethernet->frame_buffer;
	
	while((len = ethernet->packetReceive(ethernet->frame_buffer, ethernet->frame_buffer_size)) > 0)
		eth_read(frame, len);
}

static void eth_read(eth_frame_t *frame, uint16_t len)
{
	if(len > sizeof(eth_frame_t))
	{
		if(frame->type == ETH_ARP)
			if(arp_read(frame, len - sizeof(eth_frame_t)))
				arp_send(frame);			
		
		if(frame->type == ETH_IP)	
			ip_read(frame, len - sizeof(ip_pkt_t));
	}
}


static void eth_send(eth_frame_t *frame, uint16_t len)
{
	memcpy(frame->addr_dest, frame->addr_src, 6);
	memcpy(frame->addr_src, ethernet->mac_address, 6);
	ethernet->packetSend((void*)frame, len + sizeof(eth_frame_t));
	
	/*printf("eth_send: %u bytes\n", len);
	{
		uint16_t i;
		char * txt = (void*)frame;
		for (i = 0; i < len; i++)
			printf("0x%.2X ", txt[i]);
		printf("\n");
	}*/
}

static uint8_t arp_read(eth_frame_t *frame, uint16_t len)
{
	uint8_t res = 0;
	arp_pkt_t *msg = (void*)frame->data;
	if(len > sizeof(arp_pkt_t))
		if((msg->net_tp == ARP_ETH) && (msg->proto_tp == ARP_IP))
			if((msg->op == ARP_REQUEST) && (!memcmp(msg->ipaddr_dst, ethernet->ipaddr, 4)))
				res = 1;
	return res;
}

static void arp_send(eth_frame_t *frame)
{
	arp_pkt_t *msg=(void*)frame->data;
	msg->op = ARP_REPLY;
	memcpy(msg->macaddr_dst, msg->macaddr_src, 6);
	memcpy(msg->macaddr_src, ethernet->mac_address, 6);
	memcpy(msg->ipaddr_dst, msg->ipaddr_src, 4);
	memcpy(msg->ipaddr_src, ethernet->ipaddr, 4);
	eth_send(frame, sizeof(arp_pkt_t));
}

static uint8_t ip_send(eth_frame_t *frame, uint16_t len)
{
	uint8_t res = 0;
	ip_pkt_t *ip_pkt = (void*)frame->data;
	//fullfill header of IP package
	ip_pkt->len = be16toword(len);
	ip_pkt->fl_frg_of = 0;
	ip_pkt->ttl = 128;
	ip_pkt->cs = 0;
	memcpy(ip_pkt->ipaddr_dst, ip_pkt->ipaddr_src, 4);
	memcpy(ip_pkt->ipaddr_src, ethernet->ipaddr, 4);
	ip_pkt->cs = checksum((void*)ip_pkt, sizeof(ip_pkt_t));
	//send frame
	eth_send(frame, len);
	return res;
}

static uint8_t ip_read(eth_frame_t *frame, uint16_t len)
{
	uint8_t res = 0;
	ip_pkt_t *ip_pkt = (void*)frame->data;
	if((ip_pkt->verlen == 0x45) && (!memcmp(ip_pkt->ipaddr_dst, ethernet->ipaddr, 4)))
	{
		len = be16toword(ip_pkt->len) - sizeof(ip_pkt_t);
		switch (ip_pkt->prt)
		{
			case IP_ICMP:	icmp_read(frame, len); break;
			case IP_TCP:	if (ethernet->tcp_handler) ethernet->tcp_handler(tcp_receive(ip_pkt->data, ip_pkt->len)); break;
			case IP_UDP:	if (ethernet->udp_handler) ethernet->udp_handler(udp_receive(ip_pkt->data, ip_pkt->len)); break;
		}
	}
	return res;
}

static uint8_t icmp_read(eth_frame_t *frame, uint16_t len)
{
	uint8_t res = 0;
	ip_pkt_t *ip_pkt = (void*)frame->data;
	icmp_pkt_t *icmp_pkt = (void*)(ip_pkt->data);
	if (len < sizeof(icmp_pkt_t)) return 0;
	
	if (icmp_pkt->msg_tp == ICMP_REQ) //answer for incoming ping
	{
		icmp_pkt->msg_tp = ICMP_REPLY;
		icmp_pkt->cs = 0;
		icmp_pkt->cs = checksum((void*)icmp_pkt, len);
		ip_send(frame, len + sizeof(ip_pkt_t));
	}
	return res;
}

uint8_t * get_mac (uint8_t * ip_address, uint32_t timeout)
{
	uint32_t counter = 0;
	eth_frame_t * frame = (void*)ethernet->frame_buffer;
	
	arp_pkt_t * arp_pkt = (void*)frame->data;
	arp_pkt->net_tp = 0x0100;
	arp_pkt->proto_tp = 0x0008;
	arp_pkt->macaddr_len = 6;
	arp_pkt->ipaddr_len = 4;
	arp_pkt->op = 0x0100;
	memcpy(arp_pkt->macaddr_src, ethernet->mac_address, 6);
	memcpy(arp_pkt->ipaddr_src, ethernet->ipaddr, 4);
	memset(arp_pkt->macaddr_dst, 0x00, 6);
	memcpy(arp_pkt->ipaddr_dst, ip_address, 4);

	memcpy(frame->addr_src, ethernet->mac_address, 6);
	memset(frame->addr_dest, 0xFF, 6);
	frame->type = ETH_ARP;
	ethernet->packetSend((void*)frame, sizeof(arp_pkt_t) + sizeof(eth_frame_t));

	while(counter++ < timeout)
		if (ethernet->packetReceive(ethernet->frame_buffer, ethernet->frame_buffer_size) > 0)
			if (arp_pkt->op == 0x0200) return arp_pkt->macaddr_src;
	
	return 0;
}

uint32_t ping (uint8_t * ip_address, uint32_t timeout)
{
	const uint8_t payload[32] = "abcdefghijklmnopqrstuvwabcdefghi";
	uint32_t counter = 0;
	
	/* icmp -> ip -> ethernet */
	eth_frame_t * frame = (void*)ethernet->frame_buffer;
	ip_pkt_t *ip_pkt = (void*)frame->data;
	icmp_pkt_t *icmp_pkt = (void*)ip_pkt->data;
	
	/* get phisical address */
	uint8_t * dest_address = get_mac(ip_address, timeout);
	if (dest_address == 0) return 0;
	
	/* prepare ethernet frame */
	if (frame->addr_dest != dest_address)
		memcpy(frame->addr_dest, dest_address, 6);	
	memcpy(frame->addr_src, ethernet->mac_address, 6);	
	frame->type = ETH_IP;
	
	/* ip */
	ip_pkt->verlen = 0x45;
	ip_pkt->ts = 0x00;
	ip_pkt->len = be16toword(sizeof(icmp_pkt) + sizeof(ip_pkt_t) + sizeof(payload) + 4);
	ip_pkt->id = ++id;
	ip_pkt->fl_frg_of = 0x0000;
	ip_pkt->ttl = 128;
	ip_pkt->prt = IP_ICMP;
	ip_pkt->cs = 0;
	memcpy(ip_pkt->ipaddr_src, ethernet->ipaddr, 4);
	memcpy(ip_pkt->ipaddr_dst, ip_address, 4);
	ip_pkt->cs = checksum((void*)ip_pkt, sizeof(ip_pkt_t));
	
	/* icmp */
	icmp_pkt->msg_tp = ICMP_REQ;
	icmp_pkt->msg_cd = 0x00;
	icmp_pkt->cs = 0;
	icmp_pkt->id = id;
	icmp_pkt->num = id;
	memcpy(icmp_pkt->data, (void*)payload, sizeof(payload));
	icmp_pkt->cs = checksum((void*)icmp_pkt, sizeof(icmp_pkt_t) + sizeof(payload));
	
	ethernet->packetSend((void*)frame, sizeof(icmp_pkt) + sizeof(ip_pkt_t) + sizeof(eth_frame_t) + sizeof(payload) + 4);
	
	while(counter++ < timeout)
		if (ethernet->packetReceive(ethernet->frame_buffer, ethernet->frame_buffer_size) > 0)
			if (icmp_pkt->msg_tp == ICMP_REPLY) return counter;
		
	return 0;
}

static udp_packet_t * udp_receive (uint8_t * buf, uint16_t len)
{
	udp_packet_t* res = (void*)buf;

	res->source_port = be16toword(res->source_port);
	res->dest_port = be16toword(res->dest_port);
	res->len = be16toword(res->len) - sizeof(udp_packet_t);
	res->data[res->len] = 0; //null terminate
	
	return res;
}

static tcp_packet_t * tcp_receive (uint8_t * buf, uint16_t len)
{
	eth_frame_t *frame = (void*)ethernet->frame_buffer;
	tcp_packet_t* tcp_packet = (void*)buf;
	
	uint16_t port = tcp_packet->dest_port;
	tcp_packet->dest_port = tcp_packet->source_port;
	tcp_packet->source_port = port;
	
	if (tcp_packet->flags == 2) //SYN
	{
		tcp_packet->flags = TCP_SYN | TCP_ACK;
		tcp_packet->crc16 = 0;
		tcp_packet->crc16 = checksum((void*)tcp_packet, sizeof(tcp_packet_t));
		ip_send(frame, sizeof(tcp_packet_t) + sizeof(ip_pkt_t));
	}
	
	tcp_packet->source_port = be16toword(tcp_packet->source_port);
	tcp_packet->dest_port = be16toword(tcp_packet->dest_port);
	tcp_packet->header_len = (tcp_packet->header_len & 0xF0) >> 4;
	tcp_packet->window_size = be16toword(tcp_packet->window_size);
	tcp_packet->crc16 = be16toword(tcp_packet->crc16);
	
	return tcp_packet;
}

void udp_send (udp_packet_t * udp_packet)
{
	eth_frame_t * frame = (void*)ethernet->frame_buffer;
	ip_pkt_t *ip_pkt = (void*)frame->data;
	uint16_t port, len = udp_packet->len + sizeof(ip_pkt_t) + sizeof(udp_packet_t);
	
	ip_pkt->prt = IP_UDP;
	ip_pkt->verlen = 0x45;
	
	port = udp_packet->source_port;
	udp_packet->source_port = udp_packet->dest_port;
	udp_packet->dest_port = port;
	
	udp_packet->source_port = be16toword(udp_packet->source_port);
	udp_packet->dest_port = be16toword(udp_packet->dest_port);
	udp_packet->len = be16toword(udp_packet->len + sizeof(udp_packet_t));
	udp_packet->crc16 = 0;
	//udp_packet->crc16 = checksum((void*)udp_packet, len + sizeof(udp_packet_t)); CRC disabled

	if (ip_pkt->data != (void*)udp_packet)
		memcpy(ip_pkt->data, (void*)udp_packet, udp_packet->len + sizeof(udp_packet_t));
	
	ip_send(frame, len);
}

static uint16_t checksum(uint8_t *ptr, uint16_t len)
{
	uint32_t sum = 0;
	while(len > 0)
	{
		sum += (uint16_t) (((uint32_t)*ptr << 8) | *(ptr + 1));
		ptr += 2;
		len -= 2;
	}
	if(len) sum += ((uint32_t)*ptr) << 8;
	while(sum >> 16) sum = (uint16_t)sum + (sum >> 16);
	return ~be16toword((uint16_t)sum);
}