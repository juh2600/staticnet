/***********************************************************************************************************************
*                                                                                                                      *
* staticnet                                                                                                            *
*                                                                                                                      *
* Copyright (c) 2021-2024 Andrew D. Zonenberg and contributors                                                         *
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

/**
	@file
	@brief Declaration of TCPProtocol
 */

#ifndef TCPProtocol_h
#define TCPProtocol_h

#include "TCPSegment.h"

//Default of 4 pending TCP segments allowed in flight
#ifndef TCP_MAX_UNACKED
#define TCP_MAX_UNACKED 4
#endif

#ifndef TCP_RETRANSMIT_TIMEOUT
#define TCP_RETRANSMIT_TIMEOUT 2
#endif

class TCPSentSegment
{
public:
	TCPSentSegment(TCPSegment* seg = nullptr)
	: m_segment(seg)
	, m_agingTicks(0)
	{}

	TCPSegment* m_segment;
	uint32_t m_agingTicks;
};

/**
	@brief A single entry in the TCP socket table
 */
class TCPTableEntry
{
public:
	TCPTableEntry()
	: m_valid(false)
	, m_remoteSeqSent(0)
	{
	}

	bool m_valid;
	IPv4Address m_remoteIP;
	uint16_t m_localPort;
	uint16_t m_remotePort;

	/**
		@brief Expected sequence number of the next incoming packet.

		This is the most recent ACK number we sent (or are going to send)
	 */
	uint32_t m_remoteSeq;

	///@brief Most recent sequence number we sent
	uint32_t m_localSeq;

	/**
		@brief Most recent ACK number *actually sent*
	 */
	uint32_t m_remoteSeqSent;

	///@brief Initial sequence number sent by us
	uint32_t m_localInitialSeq;

	///@brief Initial sequence number sent by remote side
	uint32_t m_remoteInitialSeq;

	//TODO: aging for session idle closure

	///@brief List of frames that have been sent but not ACKed
	TCPSentSegment m_unackedFrames[TCP_MAX_UNACKED];
};

/**
	@brief A single bank of the TCP socket table (direct mapped)
 */
class TCPTableWay
{
public:
	TCPTableEntry m_lines[TCP_TABLE_LINES];
};

#define TCP_IPV4_PAYLOAD_MTU (IPV4_PAYLOAD_MTU - 20)

/**
	@brief TCP protocol driver
 */
class TCPProtocol
{
public:
	TCPProtocol(IPv4Protocol* ipv4);
	//TODO: IPv6 backend

	bool IsTxBufferAvailable()
	{ return m_ipv4->IsTxBufferAvailable(); }

	void OnRxPacket(
		TCPSegment* segment,
		uint16_t ipPayloadLength,
		IPv4Address sourceAddress,
		uint16_t pseudoHeaderChecksum);

	virtual void OnAgingTick10x();

	TCPSegment* GetTxSegment(TCPTableEntry* state);

	/**
		@brief Sends a TCP segment on a given socket handle
	 */
	void SendTxSegment(TCPTableEntry* state, TCPSegment* segment, uint16_t payloadLength)
	{
		//Update the socket state to expect a new ACK number in response to this segment
		auto packet = reinterpret_cast<IPv4Packet*>(reinterpret_cast<uint8_t*>(segment) - sizeof(IPv4Packet));
		state->m_localSeq += payloadLength;

		//Add the PSH flag since this segment contains data
		segment->m_offsetAndFlags |= TCPSegment::FLAG_PSH;

		//Reay to send
		SendSegment(state, segment, packet, payloadLength + sizeof(TCPSegment));
	}

	///@brief Cancels sending of a packet
	void CancelTxSegment(TCPSegment* segment, TCPTableEntry* state);

	///@brief Close a socket from the server side
	void CloseSocket(TCPTableEntry* state);

protected:
	virtual bool IsPortOpen(uint16_t port);

	/**
		@brief Generates a random initial sequence number for a new socket.

		This should be overridden in derived classes to use the best randomness available (hardware RNG etc).
	 */
	virtual uint32_t GenerateInitialSequenceNumber() =0;

	virtual void OnRxData(TCPTableEntry* state, uint8_t* payload, uint16_t payloadLen);
	virtual void OnConnectionAccepted(TCPTableEntry* state);
	virtual void OnConnectionClosed(TCPTableEntry* state);

protected:
	void OnRxSYN(TCPSegment* segment, IPv4Address sourceAddress);
	void OnRxRST(TCPSegment* segment, IPv4Address sourceAddress);
	void OnRxACK(TCPSegment* segment, IPv4Address sourceAddress, uint16_t payloadLen);

	uint16_t Hash(IPv4Address ip, uint16_t localPort, uint16_t remotePort);

	TCPTableEntry* AllocateSocketHandle(uint16_t hash);
	TCPTableEntry* GetSocketState(IPv4Address ip, uint16_t localPort, uint16_t remotePort);
	IPv4Packet* CreateReply(TCPTableEntry* state);

	void SendSegment(TCPTableEntry* state, TCPSegment* segment, IPv4Packet* packet, uint16_t length = sizeof(TCPSegment));

	///@brief The IPv4 protocol stack
	IPv4Protocol* m_ipv4;

	///@brief The socket state table
	TCPTableWay m_socketTable[TCP_TABLE_WAYS];
};

#endif
