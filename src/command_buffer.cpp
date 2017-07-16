#include <cstdlib>
#include "command_buffer.h"
#include "protocol_executor.h"

bool CommandBuffer::EnqueueCommandForReliableConnectionV( const char *format, va_list va ) {
	message.Clear();
	message.WriteByte( CLC_CLIENT_COMMAND );
	sequenceNum++;
	message.VPrintf( format, va );

	executor->channel.SendMessage( message );
	executor->lastSentAt = system->Millis();
	return true;
}

void CommandBuffer::SendHeadBuffer() {
	Message &channelMessage = executor->channel.PrepareSequencedOutgoingMessage();

	assert( numBuffers );
	assert( headBufferIndex < MAX_BUFFERS );
	buffers[headBufferIndex].message.CopyTo( channelMessage );
	executor->channel.SendMessage( channelMessage );
	buffers[headBufferIndex].lastSentAt = (int64_t)system->Millis();
}

void CommandBuffer::ResendBufferedMessages() {
	if( !numBuffers || system->Millis() < buffers[headBufferIndex].lastSentAt + TIMEOUT ) {
		return;
	}

	SendHeadBuffer();
}

Message *CommandBuffer::NewBufferedMessage() {
	if( numBuffers == MAX_BUFFERS ) {
		// ??? Do not really know whats going here... Inspect the actual client sources...
		TryAcknowledge( buffers[headBufferIndex].lastSequenceNum );

		if( numBuffers == MAX_BUFFERS ) {
			return nullptr;
		}
	}

	assert( numBuffers < MAX_BUFFERS );
	numBuffers++;
	headBufferIndex = ( headBufferIndex + 1 ) % MAX_BUFFERS;
	MessageBuffer *buffer = &buffers[headBufferIndex];
	buffer->lastSequenceNum = sequenceNum;
	buffer->lastSentAt = -TIMEOUT;
	buffer->message.Clear();
	return &buffer->message;
}

bool CommandBuffer::PushNewBufferedMessage( Message *message ) {
	assert( numBuffers );
	assert( headBufferIndex < MAX_BUFFERS );
	assert( message == &buffers[headBufferIndex].message );

	if( numBuffers == 1 ) {
		SendHeadBuffer();
	}

	return true;
}

void CommandBuffer::TryAcknowledge( int64_t ackNum ) {
	if( !numBuffers || buffers[headBufferIndex].lastSequenceNum != ackNum ) {
		return;
	}

	numBuffers--;
	headBufferIndex = ( headBufferIndex + 1 ) % MAX_BUFFERS;

	ResendBufferedMessages();
}

bool CommandBuffer::EnqueueCommandForUnreliableConnectionV( const char *format, va_list va ) {
	sequenceNum++;
	Message *message = NewBufferedMessage();

	if( !message ) {
		return false;
	}

	message->Clear();
	message->WriteByte( CLC_CLIENT_COMMAND );
	message->WriteLong( sequenceNum );
	message->VPrintf( format, va );

	return PushNewBufferedMessage( message );
}

void CommandBuffer::Reset() {
	sequenceNum = 0;
	numBuffers = 0;
	headBufferIndex = 0;
}