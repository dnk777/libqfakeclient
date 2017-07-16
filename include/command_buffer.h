#ifndef LIBQFAKECLIENT_COMMAND_BUFFER_H
#define LIBQFAKECLIENT_COMMAND_BUFFER_H

#include "channel.h"

class GenericClientProtocolExecutor;

class CommandBuffer
{
	Message message;
	int sequenceNum;

	static constexpr auto MAX_BUFFERS = 32;

	struct MessageBuffer {
		Message message;
		int64_t lastSentAt;
		int64_t lastSequenceNum;
	};

	MessageBuffer buffers[MAX_BUFFERS];
	unsigned numBuffers;
	unsigned headBufferIndex;

	Console *console;
	System *system;
	GenericClientProtocolExecutor *executor;

	void SendHeadBuffer();
	Message *NewBufferedMessage();
	bool PushNewBufferedMessage( Message *message );

public:
	CommandBuffer( Console *console_, System *system_, GenericClientProtocolExecutor *executor_ )
		: console( console_ ), system( system_ ), executor( executor_ ) {
		Reset();
	}

	void TryAcknowledge( int64_t ackNum );
	void ResendBufferedMessages();

	bool EnqueueCommandForReliableConnectionV( const char *format, va_list va );
	bool EnqueueCommandForUnreliableConnectionV( const char *format, va_list va );

	void Reset();
};

#endif
