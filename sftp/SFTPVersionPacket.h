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

#ifndef SFTPVersionPacket_h
#define SFTPVersionPacket_h

class __attribute__((packed)) SFTPVersionPacket
{
public:

	uint32_t	m_version;

	//Extensions are string pairs of (name, data)
	//For now, don't advertise support for any extensions
	uint32_t	m_extension0NameLen;
	char		m_extension0NameData[18];
	uint32_t	m_extension0VersionLen;
	char		m_extension0VersionData;

	SFTPVersionPacket()
	{
		m_extension0NameLen = sizeof(m_extension0NameData);
		memcpy(m_extension0NameData, "limits@openssh.com", sizeof(m_extension0NameData));
		m_extension0VersionLen	= sizeof(m_extension0VersionData);
		m_extension0VersionData = '1';
	}

	void ByteSwap()
	{
		m_version = __builtin_bswap32(m_version);
		m_extension0NameLen = __builtin_bswap32(m_extension0NameLen);
		m_extension0VersionLen = __builtin_bswap32(m_extension0VersionLen);
	}


};

#endif
