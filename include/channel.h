#ifndef LIBQFAKECLIENT_CHANNEL_H
#define LIBQFAKECLIENT_CHANNEL_H

#include "common.h"
#include "network_address.h"
#include "system.h"

#include <stdint.h>

class Console;
class Socket;

class Message
{
	friend class Channel;
	friend class CommandBuffer;

	Console *console;

	// TODO: Allocate dynamically depending of a the protocol version?
	uint8_t buffer[MAX_MSGLEN];
	char stringBuffer[MAX_MSG_STRING_CHARS + 1];

	unsigned maxSize;
	unsigned currSize;
	unsigned readCount;
	unsigned sequenceNum;

public:
	Message() {
		console = System::Instance()->SystemConsole();
	}

	unsigned CurrSize() const { return currSize; }
	unsigned ReadCount() const { return readCount; }
	unsigned BytesLeft() const {
		return readCount <= currSize ? currSize - readCount : 0;
	}
	void SetReadCount( unsigned readCount_ ) { this->readCount = readCount_; }
	inline uint8_t *Buffer() { return buffer; }

	bool IsSequential() {
		// An integer containing the fragment bit is never a valid sequence num
		return !( sequenceNum & FRAGMENT_BIT );
	}

	void Clear() {
		maxSize = MAX_MSGLEN;
		currSize = 0;
		readCount = 0;
	}

	int ReadChar();
	int ReadByte();
	int ReadShort();
	int ReadLong();
	int ReadInt3();
	const char *ReadString();
	void ReadData( void *buffer, unsigned length );
	bool Skip( unsigned length );

	void WriteChar( int c );
	void WriteByte( int c );
	void WriteShort( int c );
	void WriteLong( int c );
	void WriteInt3( int c );
	void WriteFloat( float f );
	void WriteData( const void *buffer, unsigned length );

	void WriteString( const char *string );

#ifndef _MSC_VER
	void Printf( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
#else
	void Printf( _Printf_format_string_ const char *format, ... );
#endif
	void VPrintf( const char *format, va_list va );

	void CopyTo( Message &output );
};

inline void Message::Printf( const char *format, ... ) {
	va_list va;

	va_start( va, format );
	this->VPrintf( format, va );
	va_end( va );
}

class ChannelListener
{
public:
	virtual void OnIngoingSequencedMessage( Message &message ) = 0;
	virtual void OnIngoingNonSequencedMessage( Message &message ) = 0;
};

class Channel
{
	friend class Client;
	friend class CommandBuffer;

	Console *console;
	System *system;
	Socket *socket;
	ChannelListener *listener;

	int ingoingSequenceNum;
	int outgoingSequenceNum;
	uint16_t natPunchthroughPort;

	int totalFragmentSize;
	uint8_t fragmentBuffer[MAX_MSGLEN];

	Message ingoingMessage;
	Message outgoingMessage;

	NetworkAddress currServerAddress;
	bool PrepareSocket( const NetworkAddress &address );

	static void ListeningCallback( void *channel, const NetworkAddress &address, unsigned dataSize );

	void SendMessage( const Message &message );

public:
	Channel( Console *console_, System *system_, ChannelListener *listener_ )
		: console( console_ ), system( system_ ), listener( listener_ ), socket( nullptr ) {}

	uint16_t NatPunchthroughPort() const { return natPunchthroughPort; }

	void Send() {
		SendMessage( outgoingMessage );
	}
	void Receive( const NetworkAddress &from, const uint8_t *data, unsigned dataSize );

	Message &PrepareSequencedOutgoingMessage();
	Message &PrepareNonSequencedOutgoingMessage();

	void StartListening();
	void StopListening();
	void Reset();

	bool PrepareForAddress( const NetworkAddress &address );
};

#endif
