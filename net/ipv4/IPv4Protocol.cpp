/***********************************************************************************************************************
*                                                                                                                      *
* staticnet v0.1                                                                                                       *
*                                                                                                                      *
* Copyright (c) 2021 Andrew D. Zonenberg and contributors                                                              *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

#include <stdio.h>

#include <staticnet-config.h>
#include <stack/staticnet.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

/**
	@brief Initializes the IPv4 protocol stack
 */
IPv4Protocol::IPv4Protocol(EthernetProtocol& eth, IPv4Config& config, ARPCache& cache)
	: m_eth(eth)
	, m_config(config)
	, m_cache(cache)
	, m_icmpv4(NULL)
	, m_tcp(NULL)
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checksum calculation

/**
	@brief Computes the Internet Checksum on a block of data in network byte order.
 */
uint16_t IPv4Protocol::InternetChecksum(uint8_t* data, uint16_t len, uint16_t initial)
{
	//Sum in 16-bit blocks until we run out
	uint16_t* data16 = reinterpret_cast<uint16_t*>(data);
	uint32_t checksum = initial;
	while(len >= 2)
	{
		//Add with carry
		checksum += __builtin_bswap16(*data16);
		checksum = (checksum >> 16) + (checksum & 0xffff);

		data16 ++;
		len -= 2;
	}

	//Add the last byte if needed
	if(len & 1)
	{
		checksum += __builtin_bswap16(*reinterpret_cast<uint8_t*>(data16));
		checksum = (checksum >> 16) + (checksum & 0xffff);
	}
	return checksum;
}

/**
	@brief Calculates the TCP/UDP pseudoheader checksum for a packet
 */
uint16_t IPv4Protocol::PseudoHeaderChecksum(IPv4Packet* packet, uint16_t length)
{
	uint8_t pseudoheader[]
	{
		packet->m_sourceAddress.m_octets[0],
		packet->m_sourceAddress.m_octets[1],
		packet->m_sourceAddress.m_octets[2],
		packet->m_sourceAddress.m_octets[3],

		packet->m_destAddress.m_octets[0],
		packet->m_destAddress.m_octets[1],
		packet->m_destAddress.m_octets[2],
		packet->m_destAddress.m_octets[3],

		0x0,
		packet->m_protocol,
		static_cast<uint8_t>(length >> 8),
		static_cast<uint8_t>(length & 0xff)
	};

	return InternetChecksum(pseudoheader, sizeof(pseudoheader));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Routing helpers

/**
	@brief Figures out if an address is a unicast to us, a broad/multicast, or something else
 */
IPv4Protocol::AddressType IPv4Protocol::GetAddressType(IPv4Address addr)
{
	if(addr == m_config.m_address)
		return ADDR_UNICAST_US;
	else if(addr == m_config.m_broadcast)
		return ADDR_BROADCAST;
	else if(addr == IPv4Address{.m_octets{255, 255, 255, 255}} )
		return ADDR_BROADCAST;
	else if((addr.m_octets[0] & 0xf0) == 0xe0)
		return ADDR_MULTICAST;
	else
		return ADDR_UNICAST_OTHER;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handler for incoming packets

/**
	@brief Handle an incoming IPv4 packet
 */
void IPv4Protocol::OnRxPacket(IPv4Packet* packet, uint16_t ethernetPayloadLength)
{
	//Compute the checksum before doing byte swapping, since it expects network byte order
	//OK to do this before sanity checking the length, because the packet buffer is always a full MTU in size.
	//Worst case a corrupted length field will lead to us checksumming garbage data after the end of the packet,
	//but it's guaranteed to be a readable memory address.
	if(0xffff != InternetChecksum(reinterpret_cast<uint8_t*>(packet), packet->HeaderLength()))
		return;

	//Swap header fields to host byte order
	packet->ByteSwap();

	//Must be a well formed packet with no header options
	if(packet->m_versionAndHeaderLen != 0x45)
		return;

	//ignore DSCP / ECN

	//Length must be plausible (enough to hold headers and not more than the received packet size)
	if( (packet->m_totalLength < 20) || (packet->m_totalLength > ethernetPayloadLength) )
		return;

	//Ignore fragment ID

	//Flags must have evil bit and more-fragments bit clear, and no frag offset (not a fragment)
	//Ignore DF bit.
	if( (packet->m_flagsFragOffHigh & 0xbf) != 0)
		return;
	if(packet->m_fragOffLow != 0)
		return;

	//Ignore TTL

	//Header checksum is already validated

	//See what dest address is. It should be us, multicast, or broadcast.
	//Discard any packet that isn't for an address we care about
	//TODO: discard anything directed to a multicast group we're not interested in?
	auto type = GetAddressType(packet->m_destAddress );
	if(type == ADDR_UNICAST_OTHER)
		return;

	//Figure out the upper layer protocol
	uint16_t plen = packet->PayloadLength();
	switch(packet->m_protocol)
	{
		//We respond to pings sent to unicast or broadcast addresses only.
		//Ignore any multicast destinations for ICMP traffic.
		case IP_PROTO_ICMP:
			if(m_icmpv4 && ( (type == ADDR_UNICAST_US) || (type == ADDR_BROADCAST) ) )
			{
				m_icmpv4->OnRxPacket(
					reinterpret_cast<ICMPv4Packet*>(packet->Payload()),
					packet->PayloadLength(),
					packet->m_sourceAddress);
			}

			break;

		//TCP segments must be directed at our unicast address.
		//The connection oriented flow makes no sense to be broadcast/multicast.
		case IP_PROTO_TCP:
			if(m_tcp && (type == ADDR_UNICAST_US) )
			{
				m_tcp->OnRxPacket(
					reinterpret_cast<TCPSegment*>(packet->Payload()),
					plen,
					packet->m_sourceAddress,
					PseudoHeaderChecksum(packet, plen));
			}
			break;

		//TODO: handle UDP traffic
		case IP_PROTO_UDP:
			if(type == ADDR_UNICAST_US)
				printf("got UDP packet TODO\n");
			break;

		//ignore any unknown protocols
		default:
			break;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handler for outbound packets

/**
	@brief Allocates an outbound packet and prepare to send it

	Returns NULL if we don't have an ARP entry for the destination yet and it's not a broadcast
 */
IPv4Packet* IPv4Protocol::GetTxPacket(IPv4Address dest, ipproto_t proto)
{
	//Find target MAC address
	auto type = GetAddressType(dest);
	MACAddress destmac = {{0, 0, 0, 0, 0, 0}};
	switch(type)
	{
		//TODO: use well known mac for some multicasts
		//For now, just fall through to broadcast MAC
		case ADDR_MULTICAST:

		//If it's a broadcast, set it to a layer-2 broadcast MAC
		case ADDR_BROADCAST:
			destmac = MACAddress{{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};
			break;

		//Unicast? Check the ARP table
		case ADDR_UNICAST_OTHER:
			if(!m_cache.Lookup(destmac, dest))
			{
				//TODO: send ARP query for this IP so we can try again
				return NULL;
			}
			break;

		//invalid destination (can't send to ourself)
		default:
			return NULL;
	}

	//Allocate the frame and fill headers
	auto frame = m_eth.GetTxFrame(ETHERTYPE_IPV4, destmac);
	auto reply = reinterpret_cast<IPv4Packet*>(frame->Payload());
	reply->m_versionAndHeaderLen = 0x45;
	reply->m_dscpAndECN = 0;
	reply->m_fragID = 0;
	reply->m_flagsFragOffHigh = 0x40;	//DF
	reply->m_fragOffLow = 0;
	reply->m_ttl = 0xff;
	reply->m_protocol = proto;
	reply->m_sourceAddress = m_config.m_address;
	reply->m_destAddress = dest;
	reply->m_headerChecksum = 0;

	//Done
	return reply;
}

/**
	@brief Sends a packet to the driver

	The packet MUST have been allocated by GetTxPacket().
 */
void IPv4Protocol::SendTxPacket(IPv4Packet* packet, size_t upperLayerLength)
{
	//Get the full frame given the packet
	//TODO: handle VLAN tagging?
	auto frame = reinterpret_cast<EthernetFrame*>(reinterpret_cast<uint8_t*>(packet) - ETHERNET_PAYLOAD_OFFSET);

	//Update length in both IP header and Ethernet frame metadata
	packet->m_totalLength = packet->HeaderLength() + upperLayerLength;
	frame->SetPayloadLength(packet->m_totalLength);

	//Final fixup of checksum and byte ordering before sending it out
	packet->ByteSwap();
	packet->m_headerChecksum = ~__builtin_bswap16(InternetChecksum(reinterpret_cast<uint8_t*>(packet), 20));
	m_eth.SendTxFrame(frame);
}
