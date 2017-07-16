#include "channel.h"
#include "client.h"
#include "socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <arpa/inet.h>
#include <network_address.h>

#ifndef _WIN32

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN
#define LittleShort( x ) ( x )
#define LittleLong( x ) ( x )
#define LittleFloat( x ) ( x )
#else
#define LittleShort( x ) __builtin_bswap16( x )
#define LittleLong( x ) __builtin_bswap32( x )
#define LittleFloat( x ) __bultin_bswap32( x )
#endif
#else
#error Windows version has not been implemented yet
#endif

int Message::ReadChar() {
	if( readCount < currSize ) {
		return (signed char)buffer[readCount++];
	}
	abort();
}

int Message::ReadByte() {
	if( readCount < currSize ) {
		return (signed char)buffer[readCount++];
	}
	abort();
}

int Message::ReadShort() {
	if( readCount + 2 <= currSize ) {
		unsigned byte0 = buffer[readCount + 0];
		unsigned byte1 = buffer[readCount + 1];
		readCount += 2;
		return (short)( byte0 | ( byte1 << 8 ) );
	}
	abort();
}

int Message::ReadLong() {
	if( readCount + 4 <= currSize ) {
		unsigned byte0 = buffer[readCount + 0];
		unsigned byte1 = buffer[readCount + 1];
		unsigned byte2 = buffer[readCount + 2];
		unsigned byte3 = buffer[readCount + 3];
		readCount += 4;
		return byte0 | ( byte1 << 8 ) | ( byte2 << 16 ) | ( byte3 << 24 );
	}
	abort();
}

int Message::ReadInt3() {
	if( readCount + 3 <= currSize ) {
		unsigned byte0 = buffer[readCount + 0];
		unsigned byte1 = buffer[readCount + 1];
		unsigned byte2 = buffer[readCount + 2];
		readCount += 3;
		int result = byte0 | ( byte1 << 8 ) | ( byte2 << 16 );

		if( byte0 & 0x80 ) {
			result |= -0xFFFFFF;
		}
		return result;
	}
	abort();
}

const char *Message::ReadString() {
	char *s = stringBuffer;
	ssize_t readableBytes = currSize - readCount;

	while( readableBytes > 0 && ( s - stringBuffer ) < MAX_MSG_STRING_CHARS ) {
		*s = buffer[readCount++];

		if( !*s ) {
			return stringBuffer;
		}
		s++;
		readableBytes--;
	}
	// We are sure it's legal since the buffer has an extra byte at the end
	*s = 0;
	return stringBuffer;
}

void Message::ReadData( void *buffer, unsigned length ) {
	int8_t *output = (int8_t *)buffer;
	ssize_t readableBytes = currSize - readCount;

	if( readableBytes > 0 ) {
		if( readableBytes > length ) {
			memcpy( output, this->buffer + readCount, length );
			readCount += length;
		} else {
			memcpy( output, this->buffer + readCount, (size_t)readableBytes );
			readCount += readableBytes;

			// Keep the old behaviour (fill output by -1)
			for( int i = 0; i < length - readableBytes; ++i )
				output[readableBytes + i] = (int8_t)-1;
		}
	} else {
		for( int i = 0; i < length; ++i )
			output[i] = (int8_t)-1;
	}
}

bool Message::Skip( unsigned length ) {
	ssize_t readableBytes = currSize - readCount;

	if( readableBytes >= length ) {
		readCount += length;
		return true;
	}
	return false;
}

void Message::WriteChar( int c ) {
	if( currSize < maxSize ) {
		buffer[currSize++] = (uint8_t)c;
	} else {
		console->Printf( "Message::WriteChar(): buffer overflow\n" );
	}
}

void Message::WriteByte( int c ) {
	if( currSize < maxSize ) {
		buffer[currSize++] = (uint8_t)( c & 0xFF );
	} else {
		console->Printf( "Message::WriteByte(): buffer overflow\n" );
		abort();
	}
}

void Message::WriteShort( int c ) {
	if( currSize + 1 < maxSize ) {
		buffer[currSize + 0] = (uint8_t)( c & 0xFF );
		buffer[currSize + 1] = (uint8_t)( ( c >> 8 ) & 0xFF );
		currSize += 2;
	} else {
		console->Printf( "Message::WriteShort(): buffer overflow\n" );
		abort();
	}
}

void Message::WriteLong( int c ) {
	if( currSize + 3 < maxSize ) {
		buffer[currSize + 0] = (uint8_t)( c & 0xFF );
		buffer[currSize + 1] = (uint8_t)( ( c >> 8 ) & 0xFF );
		buffer[currSize + 2] = (uint8_t)( ( c >> 16 ) & 0xFF );
		buffer[currSize + 3] = (uint8_t)( ( c >> 24 ) & 0xFF );
		currSize += 4;
	} else {
		console->Printf( "Message::WriteLong(): buffer overflow\n" );
		abort();
	}
}

void Message::WriteInt3( int c ) {
	if( currSize + 2 < maxSize ) {
		buffer[currSize + 0] = (uint8_t)( c & 0xFF );
		buffer[currSize + 1] = (uint8_t)( ( c >> 8 ) & 0xFF );
		buffer[currSize + 2] = (uint8_t)( ( c >> 16 ) & 0xFF );
	} else {
		console->Printf( "Message::WriteInt3(): buffer overflow\n" );
		abort();
	}
}

void Message::WriteFloat( float f ) {
	union {
		uint32_t i;
		float f;
	} u;

	u.f = f;

	if( currSize + 3 < maxSize ) {
		buffer[currSize + 0] = (uint8_t)( u.i & 0xFF );
		buffer[currSize + 1] = (uint8_t)( ( u.i >> 8 ) & 0xFF );
		buffer[currSize + 2] = (uint8_t)( ( u.i >> 16 ) & 0xFF );
		buffer[currSize + 3] = (uint8_t)( ( u.i >> 24 ) & 0xFF );
		currSize += 4;
	} else {
		console->Printf( "Message::WriteFloat(): buffer overflow\n" );
		abort();
	}
}

void Message::WriteData( const void *buffer, unsigned length ) {
	if( currSize + length <= maxSize ) {
		memcpy( this->buffer, buffer, length );
		currSize += length;
	} else {
		console->Printf( "Message::WriteData(): buffer overflow on an attempt to write %d bytes\n", length );
		abort();
	}
}

void Message::WriteString( const char *string ) {
	auto oldSize = currSize;

	while( currSize < maxSize && *string ) {
		buffer[currSize++] = (uint8_t)*string++;
	}

	if( !*string && currSize < maxSize ) {
		buffer[currSize++] = 0;
		return;
	}

	console->Printf( "Message::WriteString(): buffer overflow\n" );
	currSize = oldSize;
	abort();
}

void Message::VPrintf( const char *format, va_list va ) {
	// Should be signed as currSize might be less
	int bytesLeft = maxSize - currSize;

	if( bytesLeft <= 0 ) {
		console->Printf( "Message::VPrintf(): message buffer overflow\n" );
		abort();
	}
	int numChars = vsnprintf( (char *)buffer + currSize, bytesLeft - 1u, format, va );

	if( numChars < 0 ) {
		console->Printf( "Message::VPrintf(): format buffer overflow\n" );
		abort();
	}
	buffer[currSize + numChars] = 0;
	currSize += numChars + 1;
}

void Message::CopyTo( Message &output ) {
	// We can reuse WriteData() but its better to provide an exact message in case of error
	if( output.currSize + this->currSize <= output.maxSize ) {
		memcpy( output.buffer + output.currSize, this->buffer, this->currSize );
		output.currSize += this->currSize;
	} else {
		const char *fmt = "Message::CopyTo(): overflow while trying to add %d bytes in addition to present %d bytes\n";
		console->Printf( fmt, this->currSize, output.currSize );
		abort();
	}
}

Message &Channel::PrepareSequencedOutgoingMessage() {
	outgoingMessage.Clear();
	outgoingMessage.WriteLong( outgoingSequenceNum++ );
	outgoingMessage.WriteLong( ingoingSequenceNum );
	outgoingMessage.WriteShort( natPunchthroughPort );
	return outgoingMessage;
}

Message &Channel::PrepareNonSequencedOutgoingMessage() {
	outgoingMessage.Clear();
	outgoingMessage.WriteLong( -1 );
	return outgoingMessage;
}

bool Channel::PrepareForAddress( const NetworkAddress &address ) {
	if( address == currServerAddress ) {
		console->Printf( "Channel::PrepareSocket(): already using the address\n" );
		return true;
	}

	currServerAddress = address;

	if( currServerAddress != address ) {
		abort();
	}

	if( !PrepareSocket( address ) ) {
		return false;
	}

	ingoingSequenceNum = 0;
	outgoingSequenceNum = 0;
	totalFragmentSize = 0;
	currServerAddress = address;
	return true;
}

bool Channel::PrepareSocket( const NetworkAddress &address ) {
	if( socket ) {
		if( socket->IsIpV4Socket() ^ address.IsIpV4Address() ) {
			system->DeleteSocket( socket );
			socket = system->NewSocket( socket->IsIpV4Socket() );
		}
	} else {
		socket = system->NewSocket( address.IsIpV4Address() );
	}

	if( !socket ) {
		console->Printf( "Channel::PrepareSocket(): cannot create a socket\n" );
		return false;
	}

	int randomInt = rand();
	natPunchthroughPort = (uint16_t)( ( randomInt >> 16 ) ^ ( randomInt & 0xFFFF ) );

	return true;
}

void Channel::Reset() {
	StopListening();
}

void Channel::StartListening() {
	if( !socket ) {
		console->Printf( "Channel::StartListening(): there is no active socket\n" );
		return;
	}

	system->AddListenedSocket( socket, this, (uint8_t *)ingoingMessage.buffer, MAX_MSGLEN, &ListeningCallback );
}

void Channel::ListeningCallback( void *channel, const NetworkAddress &address, unsigned dataSize ) {
	( (Channel *)channel )->Receive( address, ( (Channel *)channel )->ingoingMessage.buffer, dataSize );
}

void Channel::StopListening() {
	if( socket ) {
		system->RemoveListenedSocket( socket );
		system->DeleteSocket( socket );
		socket = nullptr;
	}
}

void Channel::SendMessage( const Message &message ) {
	if( !socket ) {
		console->Printf( "Channel::Send(): there is no active socket\n" );
		return;
	}

	if( !socket->SendDatagram( currServerAddress, message.buffer, message.currSize ) ) {
		console->Printf( "Channel::SendMessage(): socket->SendDatagram() call has failed\n" );
	}
}

void Channel::Receive( const NetworkAddress &from, const uint8_t *data, unsigned dataSize ) {
	if( from != currServerAddress ) {
		return;
	}

	ingoingMessage.Clear();
	ingoingMessage.currSize = dataSize;

	int sequenceNum = ingoingMessage.ReadLong();

	if( sequenceNum == -1 ) {
		listener->OnIngoingNonSequencedMessage( ingoingMessage );
		return;
	}

	// sock->sequenced = true
	bool fragmented = false;

	if( sequenceNum & FRAGMENT_BIT ) {
		sequenceNum &= ~FRAGMENT_BIT;
		fragmented = true;
	}

	// Discard packets that are already received
	if( fragmented ) {
		if( sequenceNum < ingoingSequenceNum ) {
			return;
		}
	} else {
		if( sequenceNum <= ingoingSequenceNum ) {
			return;
		}
	}

	ingoingSequenceNum = sequenceNum;
	bool compressed = ( ingoingMessage.ReadLong() & FRAGMENT_BIT ) != 0;

	if( fragmented ) {
		int fragmentStart = ingoingMessage.ReadShort();
		int fragmentLength = ingoingMessage.ReadShort();

		// Discard a packet if a fragment has arrived out of order
		if( fragmentStart != totalFragmentSize ) {
			ingoingMessage.Clear();
			return;
		}
		bool last = false;

		if( fragmentLength & FRAGMENT_LAST ) {
			fragmentLength &= ~FRAGMENT_LAST;
			last = true;
		}
		memcpy( fragmentBuffer + totalFragmentSize, ingoingMessage.Buffer() + ingoingMessage.ReadCount(), (unsigned)fragmentLength );
		totalFragmentSize += fragmentLength;

		if( !last ) {
			ingoingMessage.Clear();
			return;
		}
		memcpy( ingoingMessage.buffer, fragmentBuffer, (unsigned)totalFragmentSize );
		ingoingMessage.readCount = 0;
		ingoingMessage.currSize = (unsigned)totalFragmentSize;
	}

	unsigned bytesLeft = ingoingMessage.currSize - ingoingMessage.readCount;

	if( compressed && bytesLeft > 0 ) {
		uint8_t tmp[MAX_MSGLEN];
		uint8_t *compressedData = ingoingMessage.buffer + ingoingMessage.readCount;
		unsigned long newSize = MAX_MSGLEN;

		if( uncompress( tmp, &newSize, compressedData, bytesLeft ) != Z_OK ) {
			// Should never happen. TODO: Add and use failure listener callbacks?
			abort();
		}
		memcpy( ingoingMessage.buffer, tmp, newSize );
		ingoingMessage.currSize = (int)newSize;
		ingoingMessage.readCount = 0;
	}

	listener->OnIngoingSequencedMessage( ingoingMessage );
}