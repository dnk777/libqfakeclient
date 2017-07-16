#include "system.h"
#include "client.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <new>

#ifndef _WIN32
#include <time.h>
#include <unistd.h>
#else
#error There is no Windows-compatible version yet
#endif

System *System::globalSystem = nullptr;

// Use this type to ensure the buffer is at least 8-byte aligned without using unportable attributes
static uint64_t globalSystemBuffer[sizeof( System ) / sizeof( uint64_t ) + 1];

Client *System::NewClient( Console *console ) {
	for( unsigned i = 0; i < MAX_FAKE_CLIENT_INSTANCES; ++i ) {
		if( !clients[i] ) {
			if( void *mem = malloc( sizeof( Client ) ) ) {
				clients[i] = new(mem)Client( console, this );
				return clients[i];
			} else {
				console->Printf( "System::NewClient(): cannot allocate memory for a client\n" );
				return nullptr;
			}
		}
	}
	return nullptr;
}

void System::DeleteClient( Client *client ) {
	if( !client ) {
		console->Printf( "System::DeleteClient(): the argument is null, the call is ignored\n" );
		return;
	}

	for( unsigned i = 0; i < MAX_FAKE_CLIENT_INSTANCES; ++i ) {
		if( clients[i] == client ) {
			clients[i] = nullptr;
			client->~Client();
			free( client );
			return;
		}
	}

	console->Printf( "System::DeleteClient(): unregistered client address\n" );
}

void System::Init( Console *systemConsole ) {
	if( globalSystem ) {
		return;
	}

	globalSystem = new (globalSystemBuffer)System( systemConsole );
}

void System::Shutdown() {
	if( !globalSystem ) {
		return;
	}

	globalSystem->~System();
	globalSystem = nullptr;
}

System::System( Console *systemConsole ) {
	console = systemConsole;
	millis = 0;

#ifndef WIN32
	timespec *timestamp = (timespec *)malloc( sizeof( timespec ) );
	this->timestamp = timestamp;
	clock_gettime( CLOCK_MONOTONIC, timestamp );
#endif

	// Ensure that the memory is zeroed before first use
	memset( clients, 0, MAX_FAKE_CLIENT_INSTANCES * sizeof( clients[0] ) );
}

System::~System() {
	for( unsigned i = 0; i < MAX_FAKE_CLIENT_INSTANCES; ++i ) {
		if( !clients[i] ) {
			continue;
		}

		clients[i]->~Client();
		free( clients[i] );
		clients[i] = nullptr;
	}
}

void System::Sleep( unsigned millis ) {
#ifndef _WIN32
	usleep( millis * 1000 );
#endif
}

bool System::AddListenedSocket( Socket *socket, void *owner, uint8_t *buffer, unsigned bufferSize,
								void ( *callback )( void *, const NetworkAddress &, unsigned ) ) {
	if( numListenedSockets == MAX_SOCKETS ) {
		console->Printf( "Can't add a listened socket: too many sockets\n" );
		return false;
	}

	for( unsigned i = 0; i < numListenedSockets; ++i ) {
		if( listenedSockets[i].socket == socket ) {
			console->Printf( "Can't add a listened socket: the same socket is already present\n" );
			return false;
		}
	}

	ListenedSocket *listenedSocket = &listenedSockets[numListenedSockets++];
	listenedSocket->socket = socket;
	listenedSocket->owner = owner;
	listenedSocket->buffer = buffer;
	listenedSocket->bufferSize = bufferSize;
	listenedSocket->callback = callback;

	return true;
}

bool System::RemoveListenedSocket( Socket *socket ) {
	for( unsigned i = 0; i < numListenedSockets; ++i ) {
		if( listenedSockets[i].socket == socket ) {
			// Replace by the last one
			listenedSockets[i] = listenedSockets[numListenedSockets - 1];
			numListenedSockets--;
			return true;
		}
	}

	console->Printf( "Can't remove a listened socket: there is no same socket in the sockets set\n" );
	return false;
}

void System::Frame( unsigned maxMillis ) {
	TimeFrame( maxMillis );
	NetPollFrame( maxMillis );
	ClientsFrame( maxMillis );
}

void System::TimeFrame( unsigned maxMillis ) {
#ifndef _WIN32
	timespec prevTimestamp = *(timespec *)this->timestamp;
	timespec *currTimestamp = (timespec *)this->timestamp;
	clock_gettime( CLOCK_MONOTONIC, currTimestamp );

	int64_t deltaNanos = currTimestamp->tv_sec * 1000 * 1000 * 1000 + currTimestamp->tv_nsec;
	deltaNanos -= prevTimestamp.tv_sec * 1000 * 1000 * 1000 + prevTimestamp.tv_nsec;
	this->millis += ( deltaNanos / ( 1000 * 1000 ) );
#endif
}

void System::ClientsFrame( unsigned maxMillis ) {
	for( Client *client: clients ) {
		if( client ) {
			client->Frame();
		}
	}
}
