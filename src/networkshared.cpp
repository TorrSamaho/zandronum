//-----------------------------------------------------------------------------
//
// Skulltag Source
// Copyright (C) 2007 Brad Carney, Benjamin Berkels
// Copyright (C) 2007-2012 Skulltag Development Team
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the Skulltag Development Team nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
// 4. Redistributions in any form must be accompanied by information on how to
//    obtain complete source code for the software and any accompanying
//    software that uses the software. The source code must either be included
//    in the distribution or be available for no more than the cost of
//    distribution plus a nominal fee, and must be freely redistributable
//    under reasonable conditions. For an executable file, complete source
//    code means the source code for all modules it contains. It does not
//    include source code for modules or files that typically accompany the
//    major components of the operating system on which the executable file
//    runs.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
//
//
// Filename: networkshared.cpp
//
// Description: Contains shared network code shared between Skulltag and its satellites (master server, statsmaker, rcon utility, etc).
//
//-----------------------------------------------------------------------------

#include "networkheaders.h"
#include "networkshared.h"
#include <sstream>
#include <errno.h>
#include <iostream>
#include <algorithm>
#include "i_system.h"

//*****************************************************************************
//	VARIABLES

// [BB] Are we measuring outbound traffic?
static	bool	g_MeasuringOutboundTraffic = false;
// [BB] Number of bytes sent by NETWORK_Write* since NETWORK_StartTrafficMeasurement() was called.
static	int		g_OutboundBytesMeasured = 0;

//*****************************************************************************
//
extern std::ostream &operator<< ( std::ostream &os, const IPStringArray &input ) {
	input.print ( os );
	return os;
}

//*****************************************************************************
//
NETBUFFER_s::NETBUFFER_s ( )
{
	this->pbData = NULL;
	this->ulMaxSize = 0;
	this->BufferType = (BUFFERTYPE_e)0;
	Clear();
}

//*****************************************************************************
//
NETBUFFER_s::NETBUFFER_s ( const NETBUFFER_s &Buffer )
{
	Init ( Buffer.ulMaxSize, Buffer.BufferType );
	Clear();

	memcpy( this->pbData, Buffer.pbData, Buffer.ulMaxSize );
	this->ByteStream.pbStream = this->pbData + ( Buffer.ByteStream.pbStream - Buffer.pbData );
	this->ByteStream.pbStreamEnd = this->pbData + ( Buffer.ByteStream.pbStreamEnd - Buffer.pbData );
	this->ByteStream.bitShift = Buffer.ByteStream.bitShift;
	if ( Buffer.ByteStream.bitBuffer != NULL )
		this->ByteStream.bitBuffer = this->ByteStream.pbStream + ( Buffer.ByteStream.bitBuffer - Buffer.ByteStream.pbStream );

	this->ulCurrentSize = Buffer.ulCurrentSize;
}

//*****************************************************************************
//
void NETBUFFER_s::Init( ULONG ulLength, BUFFERTYPE_e BufferType )
{
	memset( this, 0, sizeof( *this ));
	this->ulMaxSize = ulLength;
	this->pbData = new BYTE[ulLength];
	this->BufferType = BufferType;
}

//*****************************************************************************
//
void NETBUFFER_s::Free()
{
	if ( this->pbData )
	{
		delete[] ( this->pbData );
		this->pbData = NULL;
	}

	this->ulMaxSize = 0;
	this->BufferType = (BUFFERTYPE_e)0;
}

//*****************************************************************************
//
void NETBUFFER_s::Clear()
{
	this->ulCurrentSize = 0;
	this->ByteStream.pbStream = this->pbData;
	this->ByteStream.bitBuffer = NULL;
	this->ByteStream.bitShift = -1;
	if ( this->BufferType == BUFFERTYPE_READ )
		this->ByteStream.pbStreamEnd = this->ByteStream.pbStream;
	else
		this->ByteStream.pbStreamEnd = this->ByteStream.pbStream + this->ulMaxSize;
}

//*****************************************************************************
//
LONG NETBUFFER_s::CalcSize() const
{
	if ( this->BufferType == BUFFERTYPE_READ )
		return ( LONG( this->ByteStream.pbStreamEnd - this->ByteStream.pbStream ));
	else
		return ( LONG( this->ByteStream.pbStream - this->pbData ));
}

//*****************************************************************************
//
LONG NETBUFFER_s::WriteTo( BYTESTREAM_s &ByteStream ) const
{
	LONG bufferSize = CalcSize();
	if ( bufferSize > 0 )
		ByteStream.WriteBuffer( this->pbData, bufferSize );
	return bufferSize;
}

//*****************************************************************************
//
void BYTESTREAM_s::AdvancePointer( const int NumBytes, const bool OutboundTraffic )
{
	this->pbStream += NumBytes;

	if ( g_MeasuringOutboundTraffic && OutboundTraffic )
		g_OutboundBytesMeasured += NumBytes;
}

//*****************************************************************************
//
void NETWORK_StartTrafficMeasurement ( )
{
	g_OutboundBytesMeasured = 0;
	g_MeasuringOutboundTraffic = true;
}

//*****************************************************************************
//
int NETWORK_StopTrafficMeasurement ( )
{
	g_MeasuringOutboundTraffic = false;
	return g_OutboundBytesMeasured;
}

//================================================================================
// IO read functions
//================================================================================

//*****************************************************************************
//
BYTESTREAM_s::BYTESTREAM_s() :
	bitBuffer( NULL ),
	bitShift( -1 ) {}

//*****************************************************************************
//
void BYTESTREAM_s::EnsureBitSpace( int bits, bool writing )
{
	if ( ( bitBuffer == NULL ) || ( bitShift < 0 ) || ( bitShift + bits > 8 ) )
	{
		if ( writing )
		{
			// Not enough bits left in our current byte, we need a new one.
			WriteByte( 0 );
			bitBuffer = pbStream - 1;
		}
		else
		{
			// No room for the value in this byte, so we need a new one.
			if ( this->ReadByte() != -1 )
			{
				bitBuffer = pbStream - 1;
			}
			else
			{
				// Argh! No bytes left!
				Printf("BYTESTREAM_s::EnsureBitSpace: out of bytes to use\n");
				static BYTE fallback = 0;
				bitBuffer = &fallback;
			}
		}

		bitShift = 0;
	}
}

//*****************************************************************************
//
int BYTESTREAM_s::ReadByte()
{
	int	Byte = -1;

	if (( this->pbStream + 1 ) <= this->pbStreamEnd )
		Byte = *this->pbStream;

	// Advance the pointer.
	this->pbStream += 1;

	return ( Byte );
}

//*****************************************************************************
//
int BYTESTREAM_s::ReadShort()
{
	int	Short = -1;

	if (( this->pbStream + 2 ) <= this->pbStreamEnd )
		Short = (short)(( this->pbStream[0] ) + ( this->pbStream[1] << 8 ));

	// Advance the pointer.
	this->pbStream += 2;

	return ( Short );
}

//*****************************************************************************
//
int BYTESTREAM_s::ReadLong()
{
	int	Long = -1;

	if (( this->pbStream + 4 ) <= this->pbStreamEnd )
	{
		Long = (( this->pbStream[0] )
		+ ( this->pbStream[1] << 8 )
		+ ( this->pbStream[2] << 16 )
		+ ( this->pbStream[3] << 24 ));
	}

	// Advance the pointer.
	this->pbStream += 4;

	return ( Long );
}

//*****************************************************************************
//
float BYTESTREAM_s::ReadFloat()
{
	union
	{
		float	f;
		int		i;
	} dat;

	dat.i = this->ReadLong();
	return ( dat.f );
}

//*****************************************************************************
//
const char *BYTESTREAM_s::ReadString()
{
	int c;
	static char		s_szString[MAX_NETWORK_STRING];

	// Read in characters until we've reached the end of the string.
	ULONG ulIdx = 0;
	do
	{
		c = this->ReadByte();
		if ( c <= 0 )
			break;

		// Place this character into our string.
		// [BB] Even if we don't have enough space in s_szString, we have to fully 
		// parse the received string. Otherwise we can't continue parsing the packet.
		if ( ulIdx < MAX_NETWORK_STRING - 1 )
			s_szString[ulIdx] = static_cast<char> ( c );

		++ulIdx;

	} while ( true );

	// [BB] We may have read more chars than we can store.
	const int endIndex = ( ulIdx < MAX_NETWORK_STRING ) ? ulIdx : MAX_NETWORK_STRING - 1;
	s_szString[endIndex] = '\0';
	return ( s_szString );
}

//*****************************************************************************
//
bool BYTESTREAM_s::ReadBit()
{
	this->EnsureBitSpace( 1, false );

	// Use a bit shift to extract a bit from our current byte
	bool result = !!( *this->bitBuffer & ( 1 << this->bitShift ));
	this->bitShift++;
	return result;
}

//*****************************************************************************
//
int BYTESTREAM_s::ReadVariable()
{
	// Read two bits to form an integer 0...3
	int length = this->ReadBit();
	length |= this->ReadBit() << 1;

	// Use this length to read in an integer of variable length.
	switch ( length )
	{
	default:
	case 0: return 0;
	case 1: return this->ReadByte();
	case 2: return this->ReadShort();
	case 3: return this->ReadLong();
	}
}

//*****************************************************************************
//
int BYTESTREAM_s::ReadShortByte ( int bits )
{
	if ( bits >= 0 && bits <= 8 )
	{
		this->EnsureBitSpace( bits, false );
		int mask = ( 1 << bits ) - 1; // Create a mask to cover the bits we want.
		mask <<= this->bitShift; // Shift the mask so that it covers the correct bits.
		int result = *this->bitBuffer & mask; // Apply the shifted mask on our byte to remove unwanted bits.
		result >>= this->bitShift; // Shift the result back to start from 0.
		this->bitShift += bits; // Increase shift to mark these bits as used.
		return result;
	}
	else
	{
		return 0;
	}
}

//*****************************************************************************
//
void BYTESTREAM_s::ReadBuffer( void *buffer, size_t length )
{
	if (( this->pbStream + length ) > this->pbStreamEnd )
	{
		Printf( "BYTESTREAM_s::ReadBuffer: Overflow!\n" );
	}
	else
	{
		memcpy( buffer, this->pbStream, length );
		this->pbStream += length;
	}
}

//================================================================================
// IO write functions
//================================================================================

//*****************************************************************************
//
void BYTESTREAM_s::WriteByte( int Byte )
{
	if (( this->pbStream + 1 ) > this->pbStreamEnd )
	{
		Printf( "BYTESTREAM_s::WriteByte: Overflow!\n" );
		return;
	}

	*this->pbStream = Byte;

	// Advance the pointer.
	this->AdvancePointer ( 1, true );
}

//*****************************************************************************
//
void BYTESTREAM_s::WriteShort( int Short )
{
	if (( this->pbStream + 2 ) > this->pbStreamEnd )
	{
		Printf( "NETWORK_WriteShort: Overflow!\n" );
		return;
	}

	this->pbStream[0] = Short & 0xff;
	this->pbStream[1] = Short >> 8;

	// Advance the pointer.
	this->AdvancePointer ( 2, true );
}

//*****************************************************************************
//
void BYTESTREAM_s::WriteLong( int Long )
{
	if (( this->pbStream + 4 ) > this->pbStreamEnd )
	{
		Printf( "NETWORK_WriteLong: Overflow!\n" );
		return;
	}

	this->pbStream[0] = Long & 0xff;
	this->pbStream[1] = ( Long >> 8 ) & 0xff;
	this->pbStream[2] = ( Long >> 16 ) & 0xff;
	this->pbStream[3] = ( Long >> 24 );

	// Advance the pointer.
	this->AdvancePointer ( 4, true );
}

//*****************************************************************************
//
void BYTESTREAM_s::WriteFloat( float Float )
{
	union
	{
		float	f;
		int		l;
	} dat;

	dat.f = Float;

	this->WriteLong( dat.l );
}

//*****************************************************************************
//
void BYTESTREAM_s::WriteString( const char *pszString )
{
	if (( pszString ) && ( strlen( pszString ) > MAX_NETWORK_STRING ))
	{
		Printf( "BYTESTREAM_s::WriteString: String exceeds %d characters!\n", MAX_NETWORK_STRING );
		return;
	}

#ifdef	WIN32
	if ( pszString == NULL )
		this->WriteBuffer( "", 1 );
	else
		this->WriteBuffer( pszString, (int)( strlen( pszString )) + 1 );
#else
	if ( pszString == NULL )
		this->WriteByte( 0 );
	else
	{
		this->WriteBuffer( pszString, strlen( pszString ));
		this->WriteByte( 0 );
	}
#endif
}

//*****************************************************************************
//
void BYTESTREAM_s::WriteBuffer( const void *pvBuffer, int nLength )
{
	if (( this->pbStream + nLength ) > this->pbStreamEnd )
	{
		Printf( "NETWORK_WriteLBuffer: Overflow!\n" );
		return;
	}

	memcpy( this->pbStream, pvBuffer, nLength );

	// Advance the pointer.
	this->AdvancePointer ( nLength, true );
}

//*****************************************************************************
//
void BYTESTREAM_s::WriteHeader( int Byte )
{
	this->WriteByte( Byte );
	this->bitBuffer = NULL;
	this->bitShift = -1;
}

//*****************************************************************************
//
void BYTESTREAM_s::WriteBit( bool bit )
{
	// Add a bit to this byte
	this->EnsureBitSpace( 1, true );
	if ( bit )
		*this->bitBuffer |= 1 << this->bitShift;
	++this->bitShift;
}

//*****************************************************************************
//
void BYTESTREAM_s::WriteVariable( int value )
{
	int length;

	// Determine how long we need to send this value
	if ( value == 0 )
		length = 0; // 0 - don't bother sending it at all
	else if (( value <= 0xFF ) && ( value >= 0 ))
		length = 1; // Can be sent as a byte
	else if (( value <= 0x7FFF ) && ( value >= -0x8000 ))
		length = 2; // Can be sent as a short
	else
		length = 3; // Must be sent as a long

	// Write this length as two bits
	this->WriteBit( !!( length & 1 ) );
	this->WriteBit( !!( length & 2 ) );

	// Depending on the required length, write the value.
	switch ( length )
	{
	case 1: this->WriteByte( value ); break;
	case 2: this->WriteShort( value ); break;
	case 3: this->WriteLong( value ); break;
	}
}

//*****************************************************************************
//
void BYTESTREAM_s::WriteShortByte( int value, int bits )
{
	if (( bits < 1 ) || ( bits > 8 ))
	{
		Printf( "NETWORK_WriteShortByte: bits must be within range [1..8], got %d.\n", bits );
		return;
	}

	this->EnsureBitSpace( bits, true );
	value &= (( 1 << bits ) - 1 ); // Form a mask from the bits and trim our value using it.
	value <<= this->bitShift; // Shift the value to its proper position.
	*this->bitBuffer |= value; // Add it to the byte.
	this->bitShift += bits; // Bump the shift value accordingly.
}

//=============================================================================
// Utility/Conversion functions
//=============================================================================

//*****************************************************************************
//
NETADDRESS_s::NETADDRESS_s()
{
	Clear();
}

//*****************************************************************************
//
void NETADDRESS_s::Clear()
{
	abIP[0] = abIP[1] = abIP[2] = abIP[3] = 0;
	usPort = 0;
}

//*****************************************************************************
//
bool NETADDRESS_s::Compare ( const NETADDRESS_s& other, bool ignorePort ) const
{
	return (( abIP[0] == other.abIP[0] ) &&
		( abIP[1] == other.abIP[1] ) &&
		( abIP[2] == other.abIP[2] ) &&
		( abIP[3] == other.abIP[3] ) &&
		( ignorePort ? 1 : ( usPort == other.usPort )));
}

//*****************************************************************************
//
NETADDRESS_s::NETADDRESS_s ( const char* string, bool* ok )
{
	static bool sink;
	( ok ? *ok : sink ) = this->LoadFromString( string );
}

//*****************************************************************************
//
bool NETADDRESS_s::LoadFromString ( const char* string )
{
	struct hostent  *h;
	struct sockaddr_in sadr;
	char    *colon;
	char    copy[512];

	memset (&sadr, 0, sizeof(sadr));
	sadr.sin_family = AF_INET;

	sadr.sin_port = 0;

	strncpy (copy, string, 512-1);
	copy[512-1] = 0;

	// strip off a trailing :port if present
	for (colon = copy ; *colon ; colon++)
	{
		if (*colon == ':')
		{
			*colon = 0;
			sadr.sin_port = htons(atoi(colon+1));
			break;
		}
	}

	{
		const ULONG ulRet = inet_addr( copy );

		// If our return value is INADDR_NONE, the IP specified is not a valid IPv4 string.
		if ( ulRet == INADDR_NONE )
		{
			// If the string cannot be resolved to a valid IP address, return false.
			if (( h = gethostbyname( copy )) == NULL )
				return false;

			*(int *)&sadr.sin_addr = *(int *)h->h_addr_list[0];
		}
		else
			*(int *)&sadr.sin_addr = ulRet;
	}

	this->LoadFromSocketAddress( reinterpret_cast<sockaddr&>(sadr) );
	return true;
}

//*****************************************************************************
//
void NETADDRESS_s::LoadFromSocketAddress ( const struct sockaddr& sockaddr )
{
	const sockaddr_in ipv4 = reinterpret_cast<const sockaddr_in&> ( sockaddr );
	*( int * )&this->abIP = *(const int *)&ipv4.sin_addr;
	this->usPort = ipv4.sin_port;
}

//*****************************************************************************
//
void NETADDRESS_s::ToSocketAddress( struct sockaddr &SocketAddress ) const
{
	memset( &SocketAddress, 0, sizeof SocketAddress );

	struct sockaddr_in *ipv4 = reinterpret_cast <struct sockaddr_in *> ( &SocketAddress );
	*(int *)&ipv4->sin_addr = *(int *)&abIP;
	ipv4->sin_port = usPort;
	ipv4->sin_family = AF_INET;
}

//*****************************************************************************
//
void NETADDRESS_s::SetPort ( USHORT port )
{
	usPort = htons( port );
}

//*****************************************************************************
//
const char* NETADDRESS_s::ToString() const
{
	static char	buffer[64];
	sprintf( buffer, "%i.%i.%i.%i:%i", abIP[0], abIP[1], abIP[2], abIP[3], ntohs( usPort ));
	return ( buffer );
}

//*****************************************************************************
//
const char* NETADDRESS_s::ToStringNoPort() const
{
	static char	buffer[64];
	sprintf( buffer, "%i.%i.%i.%i", abIP[0], abIP[1], abIP[2], abIP[3] );
	return ( buffer );
}

//*****************************************************************************
//
bool NETADDRESS_s::IsSet() const
{
	return ( abIP[0] != 0 );
}

//*****************************************************************************
//
void NETADDRESS_s::WriteToStream ( BYTESTREAM_s *pByteStream, bool IncludePort ) const
{
	for ( int i = 0; i < 4; ++i )
		pByteStream->WriteByte( abIP[i] );
	if ( IncludePort )
		pByteStream->WriteShort( ntohs( usPort ));
}

//*****************************************************************************
//
void NETADDRESS_s::ReadFromStream ( BYTESTREAM_s *pByteStream, bool IncludePort )
{
	for ( int i = 0; i < 4; ++i )
		abIP[i] = pByteStream->ReadByte();
	if ( IncludePort )
		usPort = htons( pByteStream->ReadShort());
}

//*****************************************************************************
//
void IPStringArray::SetFrom ( const NETADDRESS_s &Address )
{
	for ( int i = 0; i < 4; ++i )
		itoa( Address.abIP[i], szAddress[i], 10 );
}

//*****************************************************************************
//
bool IPStringArray::SetFromString ( const char *pszAddressString )
{
	char	szCopy[16];
	char	*pszCopy;
	char	szTemp[4];
	char	*pszTemp;
	ULONG	ulIdx;
	ULONG	ulNumPeriods;

	// First, get rid of anything after the colon (if it exists).
	strncpy( szCopy, pszAddressString, 15 );
	szCopy[15] = 0;
	for ( pszCopy = szCopy; *pszCopy; pszCopy++ )
	{
		if ( *pszCopy == ':' )
		{
			*pszCopy = 0;
			break;
		}
	}

	// Next, make sure there's at least 3 periods.
	ulNumPeriods = 0;
	for ( pszCopy = szCopy; *pszCopy; pszCopy++ )
	{
		if ( *pszCopy == '.' )
			ulNumPeriods++;
	}

	// If there weren't 3 periods, then it's not a valid ban string.
	if ( ulNumPeriods != 3 )
		return ( false );

	ulIdx = 0;
	pszTemp = szTemp;
	*pszTemp = 0;
	for ( pszCopy = szCopy; *pszCopy; pszCopy++ )
	{
		if ( *pszCopy == '.' )
		{
			// Shouldn't happen.
			if ( ulIdx > 3 )
				return ( false );

			switch ( ulIdx )
			{
			case 0:

				strcpy( szAddress[0], szTemp );
				break;
			case 1:

				strcpy( szAddress[1], szTemp );
				break;
			case 2:

				strcpy( szAddress[2], szTemp );
				break;
			case 3:

				strcpy( szAddress[3], szTemp );
				break;
			}
			ulIdx++;
//			strcpy( szBan[ulIdx++], szTemp );
			pszTemp = szTemp;
		}
		else
		{
			*pszTemp++ = *pszCopy;
			*pszTemp = 0;
			if ( strlen( szTemp ) > 3 )
				return ( false );
		}
	}

	strcpy( szAddress[3], szTemp );

	// Finally, make sure each entry of our string is valid.
	for ( int i = 0; i < 4; ++i )
	{
		if ((( atoi( szAddress[i] ) < 0 ) || ( atoi( szAddress[i] ) > 255 )) && ( _stricmp( "*", szAddress[i] ) != 0 ))
			return ( false );
	}

    return ( true );
}

//*****************************************************************************
//
std::string IPADDRESSBAN_s::GetExpirationAsString( void ) const
{
	if (( tExpirationDate != 0 ) && ( tExpirationDate != -1 ))
	{
		struct tm *timeInfo = localtime( &tExpirationDate );
		char tempBuffer[32];

		strftime( tempBuffer, sizeof( tempBuffer ), "%m/%d/%Y %H:%M", timeInfo );
		return tempBuffer;
	}

	// [AK] Return an empty string for permanent bans.
	return "";
}

//--------------------------------------------------------------------------------------------------------------------------------------------------
//-- CLASSES ---------------------------------------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------------------------------------------------

//=============================================================================
// IPFileParser
//=============================================================================

bool IPFileParser::parseIPList( const char* FileName, std::vector<IPADDRESSBAN_s> &IPArray )
{
	FILE			*pFile;

	IPArray.clear();

	char curChar = 0;
	_numberOfEntries = 0;
	if (( pFile = fopen( FileName, "r" )) != NULL )
	{
		while ( true )
		{
			IPADDRESSBAN_s IP;
			IP.tExpirationDate = 0;
			ULONG oldNumberOfEntries = _numberOfEntries;
			bool parsingDone = !parseNextLine( pFile, IP, _numberOfEntries );

			if ( _errorMessage[0] != '\0' )
			{
				fclose( pFile );
				return false;
			}
			else if ( oldNumberOfEntries < _numberOfEntries )
				IPArray.push_back( IP );

			if ( parsingDone == true )
				break;
		}
	}
	else
	{
		sprintf( _errorMessage, "IPFileParser::parseIPList: could not open %s: %s\n", FileName, strerror( errno ));
		return false;
	}

	fclose( pFile );
	if ( _numberOfEntries > 0 )
		Printf( "%s: %d entr%s loaded.\n", FileName, static_cast<unsigned int>(_numberOfEntries), ( _numberOfEntries == 1 ) ? "y" : "ies" );
	return true;
}

//*****************************************************************************
//
char IPFileParser::skipWhitespace( FILE *pFile )
{
	char curChar = fgetc( pFile );
	while (( curChar == ' ' ) && ( curChar != -1 ))
		curChar = fgetc( pFile );

	return ( curChar );
}

//*****************************************************************************
//
char IPFileParser::skipComment( FILE *pFile )
{
	char curChar = fgetc( pFile );
	while (( curChar != '\r' ) && ( curChar != '\n' ) && ( curChar != -1 ))
		curChar = fgetc( pFile );

	return ( curChar );
}

//*****************************************************************************
//
bool IPFileParser::parseNextLine( FILE *pFile, IPADDRESSBAN_s &IP, ULONG &BanIdx )
{
	NETADDRESS_s	IPAddress;
	char			szIP[257];
	int				lPosition;

	lPosition = 0;
	szIP[0] = 0;

	char curChar = fgetc( pFile );

	// Skip whitespace.
	if ( curChar == ' ' )
	{
		curChar = skipWhitespace( pFile );

		if ( feof( pFile ))
		{
			return ( false );
		}
	}

	while ( 1 )
	{
		if ( curChar == '\r' || curChar == '\n' || curChar == ':' || curChar == '<' || curChar == '/' || curChar == -1 )
		{
			if ( lPosition > 0 )
			{
				if ( IP.szIP.SetFromString( szIP ))
				{
					if ( BanIdx == _listLength )
					{
						sprintf( _errorMessage, "parseNextLine: WARNING! Maximum number of IPs (%d) exceeded!\n", _listLength );
						return ( false );
					}

					// [RC] Read the expiration date.
					if ( curChar == '<' )
					{
						IP.tExpirationDate = readExpirationDate( pFile );
						curChar = fgetc( pFile );
						continue;
					}
					else
					{
						BanIdx++;

						// [BB] If there is a reason given why the IP is on the list, read it now.
						if ( curChar == ':' )
							readReason( pFile, IP.szComment, 128 );
						else
							IP.szComment[0] = 0;
						return ( true );
					}
				}
				else if ( IPAddress.LoadFromString( szIP ))
				{
					if ( BanIdx == _listLength )
					{
						sprintf( _errorMessage, "parseNextLine: WARNING! Maximum number of IPs (%d) exceeded!\n", _listLength );
						return ( false );
					}

					IP.szIP.SetFrom ( IPAddress );
					IP.tExpirationDate = 0;

					BanIdx++;
					// [BB] If there is a reason given why the IP is on the list, read it now.
					if ( curChar == ':' )
						readReason( pFile, IP.szComment, 128 );
					return ( true );
				}
				else
				{
					IP.szIP.Clear();
				}
			}

			if ( feof( pFile ))
			{
				return ( false );
			}
			// If we've hit a comment, skip until the end of the line (or the end of the file) and get out.
			else if ( curChar == ':' || curChar == '/' )
			{
				skipComment( pFile );
				return ( true );
			}
			else
				return ( true );
		}

		szIP[lPosition++] = curChar;
		szIP[lPosition] = 0;

		if ( lPosition == 256 )
		{
			return ( false );
		}

		curChar = fgetc( pFile );
	}
}

//*****************************************************************************
//
void IPFileParser::readReason( FILE *pFile, char *Reason, const int MaxReasonLength )
{
	char curChar = fgetc( pFile );
	int i = 0;
	while (( curChar != '\r' ) && ( curChar != '\n' ) && !feof( pFile ) && i < MaxReasonLength-1 )
	{
		Reason[i] = curChar;
		curChar = fgetc( pFile );
		i++;
	}
	Reason[i] = 0;
	// [BB] Check if we reached the end of the comment, if not skip the rest.
	if( ( curChar != '\r' ) && ( curChar != '\n' ) && ( curChar != -1 ) )
		skipComment( pFile );
}

//=============================================================================
// IPList
//=============================================================================

//*****************************************************************************
//
// [RC] Reads the entry's expiration date.
time_t IPFileParser::readExpirationDate( FILE *pFile )
{
	int	iMonth = 0, iDay = 0, iYear = 0, iHour = 0, iMinute = 0;

	int iResult = fscanf( pFile, "%d/%d/%d %d:%d>", &iMonth, &iDay, &iYear, &iHour, &iMinute );

	// If fewer than 5 elements (the %ds) were read, the user probably edited the file incorrectly.
	if ( iResult < 5 )
	{
		Printf("parseNextLine: WARNING! Failure to read the ban expiration date! (%d fields read)\n", iResult );
		return 0;
	}	

	// Create the time structure, based on the current time.
	time_t		tNow;
	time( &tNow );
	struct tm	*pTimeInfo = localtime( &tNow );

	// Edit the values, and stitch them into a new time.
	pTimeInfo->tm_mon = iMonth - 1;
	pTimeInfo->tm_mday = iDay;

	if ( iYear < 100 )
		pTimeInfo->tm_year = iYear + 2000;
	else
		pTimeInfo->tm_year = iYear - 1900;

	pTimeInfo->tm_hour = iHour;
	pTimeInfo->tm_min = iMinute;
	pTimeInfo->tm_sec = 0;

	return mktime( pTimeInfo );
}

//*****************************************************************************
//
void IPList::copy( IPList &destination )
{
	destination.clear( );

	for ( ULONG ulIdx = 0; ulIdx < _ipVector.size( ); ulIdx++ )
		destination.push_back( _ipVector[ulIdx] );

	destination.sort( );
}

//*****************************************************************************
//
bool IPList::clearAndLoadFromFile( const char *Filename )
{
	bool success = false;
	_filename = Filename;
	_error = "";

	IPFileParser parser( 65536 );

	success = parser.parseIPList( Filename, _ipVector );
	if ( !success )
		_error = parser.getErrorMessage();

	return success;
}

//*****************************************************************************
//
// [RC] Removes any temporary entries that have expired.
void IPList::removeExpiredEntries( void )
{
	time_t		tNow;

	time ( &tNow );
	for ( ULONG ulIdx = 0; ulIdx < _ipVector.size(); )
	{
		// If this entry isn't infinite, and expires in the past (or now), remove it.
		if (( _ipVector[ulIdx].tExpirationDate != 0 ) && ( _ipVector[ulIdx].tExpirationDate - tNow <= 0))
		{
			std::string	Message;

			Message = "Temporary ban for ";
			Message += std::string ( _ipVector[ulIdx].szIP );
			
			// Add the ban reason.
			if ( strlen( _ipVector[ulIdx].szComment ) )
				Message = Message + " (" + _ipVector[ulIdx].szComment + ")";

			Message += " has expired";

			// If the entry expired while the server was offline, say when it expired.
			if ( _ipVector[ulIdx].tExpirationDate - tNow <= -3 )
			{
				char		szDate[32];
				struct tm	*pTimeInfo;

				pTimeInfo = localtime( &_ipVector[ulIdx].tExpirationDate );
				strftime( szDate, 32, "%m/%d/%Y %H:%M", pTimeInfo);
				Message = Message + " (ended on " + szDate + ")";
			} 
				
			Printf( "%s.\n", Message.c_str() );
			removeEntry( ulIdx );
		}
		else
			ulIdx++;
	}
}

//*****************************************************************************
//
ULONG IPList::getFirstMatchingEntryIndex( const IPStringArray &szAddress ) const
{
	for ( ULONG ulIdx = 0; ulIdx < _ipVector.size(); ulIdx++ )
	{
		if ( szAddress.Matches ( _ipVector[ulIdx].szIP ) )
		{
			return ( ulIdx );
		}
	}

	return ( size() );
}

//*****************************************************************************
//
ULONG IPList::getFirstMatchingEntryIndex( const NETADDRESS_s &Address ) const
{
	IPStringArray szAddress;
	szAddress.SetFrom ( Address );
	return getFirstMatchingEntryIndex( szAddress );
}

//*****************************************************************************
//
bool IPList::isIPInList( const IPStringArray &szAddress ) const
{
	return ( getFirstMatchingEntryIndex ( szAddress ) != size() );
}

//*****************************************************************************
//
bool IPList::isIPInList( const NETADDRESS_s &Address ) const
{
	IPStringArray szAddress;
	szAddress.SetFrom ( Address );
	return isIPInList( szAddress );
}

//*****************************************************************************
//
ULONG IPList::doesEntryExist( const IPStringArray &szAddress ) const
{
	for ( ULONG ulIdx = 0; ulIdx < _ipVector.size( ); ulIdx++ )
	{
		if ( szAddress.IsEqualTo ( _ipVector[ulIdx].szIP ) )
		{
			return ( ulIdx );
		}
	}

	return ( static_cast<ULONG>(_ipVector.size()) );
}

//*****************************************************************************
//
IPADDRESSBAN_s IPList::getEntry( const ULONG ulIdx ) const
{
	if ( ulIdx >= _ipVector.size() )
	{
		IPADDRESSBAN_s	ZeroBan;
		ZeroBan.szIP.SetToZeroes();
		ZeroBan.szComment[0] = 0;
		ZeroBan.tExpirationDate = 0;

		return ( ZeroBan );
	}

	return ( _ipVector[ulIdx] );
}

//*****************************************************************************
//
ULONG IPList::getEntryIndex( const NETADDRESS_s &Address ) const
{
	IPStringArray szAddress;
	szAddress.SetFrom ( Address );
	return doesEntryExist( szAddress );
}

//*****************************************************************************
//
const char *IPList::getEntryComment( const NETADDRESS_s &Address ) const
{
	// [BB] Take IP ranges into account.
	ULONG ulIdx = getFirstMatchingEntryIndex( Address );
	return ( ulIdx < _ipVector.size( )) ? _ipVector[ulIdx].szComment : NULL;
}

//*****************************************************************************
//
time_t IPList::getEntryExpiration( const NETADDRESS_s &Address ) const
{
	// [BB] Take IP ranges into account.
	ULONG ulIdx = getFirstMatchingEntryIndex( Address );
	return ( ulIdx < _ipVector.size( )) ? _ipVector[ulIdx].tExpirationDate : 0;
}

//*****************************************************************************
//
std::string IPList::getEntryAsString( const ULONG ulIdx, bool bIncludeComment, bool bIncludeExpiration, bool bInludeNewline ) const
{
	std::stringstream entryStream;

	if ( ulIdx < _ipVector.size() )
	{
		// Address.
		entryStream << _ipVector[ulIdx].szIP;

		// Expiration date.
		if ( bIncludeExpiration && _ipVector[ulIdx].tExpirationDate != 0 && _ipVector[ulIdx].tExpirationDate != -1 )
			entryStream << "<" << _ipVector[ulIdx].GetExpirationAsString( ) << ">";

		// Comment.
		if ( bIncludeComment && _ipVector[ulIdx].szComment[0] )
			entryStream << ":" << _ipVector[ulIdx].szComment;

		// Newline.
		if ( bInludeNewline )
			entryStream << std::endl;
	}

	return entryStream.str();
}

//*****************************************************************************
//
void IPList::addEntry( const IPStringArray &szAddress, const char *pszPlayerName, const char *pszCommentUncleaned, std::string &Message, time_t tExpiration )
{
	FILE				*pFile;
	std::string			OutString;
	std::string			PlayerNameAndComment;
	char				szDate[32];
	ULONG				ulIdx;
	std::stringstream 	messageStream;

	// [BB] Before we can check whether the ban already exists, we need to build the full, cleaned comment string.

	// [BB] The comment may not contain line breaks or feeds, so we create a cleaned copy of the comment argument here.
	std::string Comment;
	// [BB] If pszCommentUncleaned starts with 0, it's empty and we don't need to copy it. The copy code assumes
	// that pszCommentUncleaned is not empty, so we may not call it in case pszCommentUncleaned is empty but not NULL.
	if ( pszCommentUncleaned && (pszCommentUncleaned[0] != 0) )
	{
		Comment = pszCommentUncleaned;
		const char CharsToRemove[] = "\n\r";
		const char *p;
		for ( p = CharsToRemove; *p != '\0'; ++p )
			Comment.erase( std::remove( Comment.begin(), Comment.end(), *p ), Comment.end() );
	}

	// Combine the player name and the shortened comment inside the buffer.
	if ( pszPlayerName && strlen( pszPlayerName ))
		PlayerNameAndComment = pszPlayerName;
	if ( Comment.empty() == false )
	{
		if ( PlayerNameAndComment.empty() == false )
			PlayerNameAndComment += ":";
		PlayerNameAndComment += Comment;
	}

	// Address is already in the list.
	ulIdx = doesEntryExist( szAddress );
	if ( ulIdx != _ipVector.size() )
	{
		messageStream << szAddress << " already exists in \"" << _filename << "\"";
		if ( ( getEntry ( ulIdx ).tExpirationDate != tExpiration ) || ( strnicmp ( getEntry ( ulIdx ).szComment, PlayerNameAndComment.c_str(), 127 ) ) )
		{
			messageStream << ". Just updating the expiration date and reason.\n";
			_ipVector[ulIdx].tExpirationDate = tExpiration;
			strncpy( _ipVector[ulIdx].szComment, PlayerNameAndComment.c_str(), 127 );
			_ipVector[ulIdx].szComment[127] = 0;
			rewriteListToFile();
		}
		else
			messageStream << " with same expiration and reason.\n";

		Message = messageStream.str();
		return;
	}

	// Add the entry and comment into memory.
	IPADDRESSBAN_s newIPEntry;
	newIPEntry.szIP.copyFrom ( szAddress );
	strncpy( newIPEntry.szComment, PlayerNameAndComment.c_str(), 127 );
	newIPEntry.szComment[127] = 0;
	newIPEntry.tExpirationDate = tExpiration;
	_ipVector.push_back( newIPEntry );

	// Finally, append the IP to the file.
	if ( (pFile = fopen( _filename.c_str(), "a" )) )
	{
		OutString = "\n";
		OutString += std::string ( szAddress );

		// [RC] Write the expiration date of this ban.
		if ( tExpiration )
		{			
			struct tm	*pTimeInfo;

			pTimeInfo = localtime( &tExpiration );
			strftime( szDate, 32, "%m/%d/%Y %H:%M", pTimeInfo);
			OutString = OutString + "<" + szDate + ">";
		}

		if ( PlayerNameAndComment.empty() == false )
			OutString = OutString + ":" + PlayerNameAndComment;
		if ( OutString.length() > 511 )
			OutString.resize(511);
		fputs( OutString.c_str(), pFile );
		fclose( pFile );

		messageStream << szAddress << " added to \"" << _filename << "\".";
		if ( tExpiration )
			messageStream << " It expires on " << szDate << ".";
		
		messageStream << "\n";
	}
	else
	{
		messageStream << "IPList::addEntry: could not open " << _filename << " for writing: " << strerror( errno ) << "\n";
	}

	Message = messageStream.str();
}

//*****************************************************************************
//
void IPList::addEntry( const char *pszIPAddress, const char *pszPlayerName, const char *pszComment, std::string &Message, time_t tExpiration )
{
	NETADDRESS_s	BanAddress;
	IPStringArray	szStringBan;

	if ( szStringBan.SetFromString( pszIPAddress ))
		addEntry( szStringBan, pszPlayerName, pszComment, Message, tExpiration );
	else if ( BanAddress.LoadFromString( pszIPAddress ))
	{
		szStringBan.SetFrom ( BanAddress );
		addEntry( szStringBan, pszPlayerName, pszComment, Message, tExpiration );
	}
	else
	{
		Message = "Invalid IP address string: ";
		Message += pszIPAddress;
		Message += "\n";
	}
}

//*****************************************************************************
//
// [RC] Removes the entry at the given index.
void IPList::removeEntry( ULONG ulEntryIdx )
{
	for ( ULONG ulIdx = ulEntryIdx; ulIdx < _ipVector.size() - 1; ulIdx++ )
			_ipVector[ulIdx] = _ipVector[ulIdx+1];

	_ipVector.pop_back();
	rewriteListToFile ();
}

//*****************************************************************************
//
// Removes the entry with the given IP. Writes a success/failure message to Message.
void IPList::removeEntry( const IPStringArray &szAddress, std::string &Message )
{
	ULONG entryIdx = doesEntryExist( szAddress );

	std::stringstream messageStream;
	messageStream << szAddress;

	if ( entryIdx < _ipVector.size() )
	{
		removeEntry( entryIdx );

		messageStream << " removed from \"" << _filename << "\".\n";
		Message = messageStream.str();
	}
	else
	{
		messageStream << " not found in \"" << _filename << "\".\n";
		Message = messageStream.str();
	}
}

//*****************************************************************************
//
// Removes the entry with the given address. Writes a success/failure message to Message.
void IPList::removeEntry( const char *pszIPAddress, std::string &Message )
{
	IPStringArray szStringBan;

	if ( szStringBan.SetFromString( pszIPAddress ))
		removeEntry( szStringBan, Message );
	else
	{
		Message = "Invalid IP address string: ";
		Message += pszIPAddress;
		Message += "\n";
	}
}

//*****************************************************************************
//
bool IPList::rewriteListToFile ()
{
	FILE		*pFile;
	std::string	outString;

	if (( pFile = fopen( _filename.c_str( ), "w" )))
	{
		for ( ULONG ulIdx = 0; ulIdx < size(); ulIdx++ )
			fputs( getEntryAsString( ulIdx ).c_str( ), pFile );

		fclose( pFile );
		return true;
	}
	else
	{
		_error = "IPList::rewriteListToFile: couldn't open " + _filename + " for writing: " + strerror( errno ) + "\n";
		return false;
	}
}

//*****************************************************************************
//
struct ASCENDINGIPSORT_S
{
	// [RC] Sorts the list ascendingly by IP, then by comment.
	bool operator()( IPADDRESSBAN_s rpStart, IPADDRESSBAN_s rpEnd )
	{
		const int cmp = rpStart.szIP.CompareForSort ( rpEnd.szIP );
		if ( cmp == 0 )
			return ( strcmp( rpEnd.szComment, rpStart.szComment ) != 0 );
		else
			return ( cmp < 0 );
	}
};

//*****************************************************************************
//
void IPList::sort()
{
	std::sort( _ipVector.begin(), _ipVector.end(), ASCENDINGIPSORT_S() );
}

//=============================================================================
// QueryIPQueue
//=============================================================================

//=============================================================================
//
// adjustHead
//
// Removes any old enties.
//
//=============================================================================

void QueryIPQueue::adjustHead( const unsigned long currentTime )
{
	while (( _queueHead != _queueTail ) && ( currentTime >= _IPQueue[_queueHead].nextAllowedTime ))
		_queueHead = ( _queueHead + 1 ) % MAX_QUERY_IPS;
}

//=============================================================================
//
// addressInQueue
//
// Returns whether the given IP is in the queue.
//
//=============================================================================

bool QueryIPQueue::addressInQueue( const NETADDRESS_s AddressFrom ) const
{
	// Search through the queue.
	for ( unsigned int i = _queueHead; i != _queueTail; i = ( i + 1 ) % MAX_QUERY_IPS )
	{
		if ( AddressFrom.CompareNoPort( _IPQueue[i].Address ))
			return true;
	}

	// Not found, or the queue is empty.
	return false;
}

//=============================================================================
//
// isFull
//
// Returns if the queue is full.
// This will only happen if the server is being attacked by a DOS with >= MAX_QUERY_IPS computers. The sever should stop processing new requests at that point.
//
//=============================================================================

bool QueryIPQueue::isFull( ) const
{
	return ((( _queueTail + 1 ) % MAX_QUERY_IPS ) == _queueHead );
}

//=============================================================================
//
// addAddress
//
// Adds the given address to the queue. (If the queue is full, an error written to errorOut)
//
//=============================================================================

void QueryIPQueue::addAddress( const NETADDRESS_s AddressFrom, const unsigned long currentTime, std::ostream *errorOut )
{
	// Add and advance the tail.
	_IPQueue[_queueTail].Address = AddressFrom;
	_IPQueue[_queueTail].nextAllowedTime = currentTime + _entryLength;
	_queueTail = ( _queueTail + 1 ) % MAX_QUERY_IPS;

	// Is the queue full?
	if ( _queueTail == _queueHead )
	{
		if ( errorOut )
			*errorOut << "WARNING! The IP flood queue is full.\n";

		_queueHead = ( _queueHead + 1 ) % MAX_QUERY_IPS; // [RC] Start removing older entries.
	}
}
