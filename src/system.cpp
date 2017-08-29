#include "system.h"
#include "client.h"
#include "server_list.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include <mutex>
#include <thread>
#include <new>

#ifndef _WIN32
#include <time.h>
#include <unistd.h>
#else
#error There is no Windows-compatible version yet
#endif

static std::recursive_mutex globalSystemMutex;
typedef std::lock_guard<std::recursive_mutex> SystemMutexLock;

static std::atomic<System *> globalSystem;

// Use this type to ensure the buffer is at least 8-byte aligned without using unportable attributes
static uint64_t globalSystemBuffer[sizeof( System ) / sizeof( uint64_t ) + 1];

Client *System::NewClient( Console *console ) {
	SystemMutexLock lock( globalSystemMutex );

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
	SystemMutexLock lock( globalSystemMutex );

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
	System *system = globalSystem.load( std::memory_order_acquire );

	if( !system ) {
		SystemMutexLock lock( globalSystemMutex );
		system = globalSystem.load( std::memory_order_relaxed );

		if( !system ) {
			system = new(globalSystemBuffer)System( systemConsole );
			globalSystem.store( system, std::memory_order_release );
		}
	}
}

void System::Shutdown() {
	System *system = globalSystem.load( std::memory_order_acquire );

	if( system ) {
		SystemMutexLock lock( globalSystemMutex );
		system = globalSystem.load( std::memory_order_relaxed );

		if( system ) {
			system->~System();
			globalSystem.store( nullptr, std::memory_order_release );
		}
	}
}

System *System::Instance() {
	System *system = globalSystem.load( std::memory_order_acquire );

	if( !system ) {
		SystemMutexLock lock( globalSystemMutex );
		system = globalSystem.load( std::memory_order_relaxed );

		if( !system ) {
			abort();
		}
	}
	return system;
}

System::System( Console *systemConsole ) {
	console = systemConsole;
	millis = 0;

#ifndef WIN32
	timespec *timestamp = (timespec *)malloc( sizeof( timespec ) );
	this->timestamp = timestamp;
	clock_gettime( CLOCK_MONOTONIC, timestamp );
#endif

	pinnedToThreadId = std::thread::id();

	// Ensure that the memory is zeroed before first use
	memset( clients, 0, MAX_FAKE_CLIENT_INSTANCES * sizeof( clients[0] ) );

	serverList = nullptr;
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

	if( console ) {
		console->~Console();
		free( console );
	}

	if( serverList ) {
		serverList->~ServerList();
		free( serverList );
	}
}

void System::Sleep( unsigned millis ) {
#ifndef _WIN32
	usleep( millis * 1000 );
#endif
}

void System::CheckThread( const char *function ) {
	if( this->pinnedToThreadId == std::this_thread::get_id() ) {
		return;
	}

	if( this->pinnedToThreadId == std::thread::id() ) {
		console->Printf( "Warning: System::CheckThread(%s): the system hasn't been pinned to a thread yet\n", function );
		return;
	}

	console->Printf( "%s: Attempt to use the System instance from different threads has been detected\n", function );
	abort();
}

bool System::AddListenedSocket( Socket *socket, void *owner, uint8_t *buffer, unsigned bufferSize,
								void ( *callback )( void *, const NetworkAddress &, unsigned ) ) {
	SystemMutexLock lock( globalSystemMutex );
	CheckThread( "System::AddListenedSocket()" );

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
	SystemMutexLock lock( globalSystemMutex );
	CheckThread( "System::RemoveListenedSocket()" );

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
	auto threadId = std::this_thread::get_id();

	if( this->pinnedToThreadId != threadId ) {
		// If the system has been already pinned to a thread
		if( this->pinnedToThreadId != std::thread::id() ) {
			// This call always fails in this case.
			CheckThread( "System::Frame()" );
		}
		this->pinnedToThreadId = threadId;
	}

	TimeFrame( maxMillis );
	NetPollFrame( maxMillis );
	ClientsFrame( maxMillis );

	if( serverList ) {
		serverList->Frame();
	}
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

bool System::AddMasterServer( const NetworkAddress &address ) {
	SystemMutexLock lock( globalSystemMutex );

	if( numMasterServers == MAX_MASTER_SERVERS ) {
		return false;
	}

	for( unsigned i = 0; i < numMasterServers; ++i ) {
		if( masterServers[i] == address ) {
			return false;
		}
	}

	masterServers[numMasterServers++] = address;
	return true;
}

bool System::RemoveMasterServer( const NetworkAddress &address ) {
	SystemMutexLock lock( globalSystemMutex );

	for( unsigned i = 0; i < numMasterServers; ++i ) {
		if( masterServers[i] == address ) {
			masterServers[i] = masterServers[--numMasterServers];
			return true;
		}
	}

	return false;
}

bool System::IsMasterServer( const NetworkAddress &address ) const {
	SystemMutexLock lock( globalSystemMutex );

	for( unsigned i = 0; i < numMasterServers; ++i ) {
		if( masterServers[i] == address ) {
			return true;
		}
	}

	return false;
}

struct SocketHolder {
	System *system;
	Socket *socket;
	SocketHolder( System *system_, Socket *socket_ ) : system( system_ ), socket( socket_ ) {}
	~SocketHolder() {
		if( this->socket ) {
			system->DeleteSocket( this->socket );
		}
	}
	Socket *Get() { return socket; }
	operator bool() {
		return socket != nullptr;
	}
	Socket *ReleaseOwnership() {
		Socket *result = this->socket;

		this->socket = nullptr;
		return result;
	}
};

bool System::StartUpdatingServerList( ServerListListener *listener ) {
	SystemMutexLock lock( globalSystemMutex );

	if( !listener ) {
		console->Printf( "System::StartUpdatingServerList(): The listener is null\n" );
		abort();
	}

	if( serverList ) {
		console->Printf( "System::StartUpdatingServerList(): Server list updated has been already enabled\n" );
		abort();
	}

	SocketHolder ipV4SocketHolder( this, NewSocket( true ) );

	if( !ipV4SocketHolder ) {
		return false;
	}
	SocketHolder ipV6SocketHolder( this, NewSocket( false ) );

	if( !ipV6SocketHolder ) {
		return false;
	}

	void *mem = malloc( sizeof( ServerList ) );

	if( !mem ) {
		console->Printf( "System::StartUpdatingServerList(): Can't allocate a memory for a server list\n" );
		return false;
	}

	Socket *ipV4Socket = ipV4SocketHolder.ReleaseOwnership();
	Socket *ipV6Socket = ipV6SocketHolder.ReleaseOwnership();
	this->serverList = new( mem )ServerList( this, ipV4Socket, ipV6Socket, PROTOCOL21, listener );

	for( Socket *socket: { ipV4Socket, ipV6Socket } ) {
		uint8_t *buffer = this->serverList->SocketBuffer();
		unsigned bufferSize = this->serverList->BufferSize();
		assert( bufferSize > 1024 );

		if( !this->AddListenedSocket( socket, serverList, buffer, bufferSize, &ServerList::SocketCallback ) ) {
			this->serverList->~ServerList();
			free( this->serverList );
			this->serverList = nullptr;
			return false;
		}
	}

	serverList->SetOptions( pendingShowEmptyServersOption, pendingShowPlayerInfoOption );
	return true;
}

void System::StopUpdatingServerList() {
	SystemMutexLock lock( globalSystemMutex );

	if( !serverList ) {
		return;
	}

	serverList->~ServerList();
	free( serverList );
	serverList = nullptr;
}

void System::SetServerListUpdateOptions( bool showEmptyServers, bool showPlayerInfo ) {
	SystemMutexLock lock( globalSystemMutex );

	// Keep duplicates of the options in all cases.
	// (Prevent losing options after StopUpdatingServerList()) calls
	pendingShowEmptyServersOption = showEmptyServers;
	pendingShowPlayerInfoOption = showPlayerInfo;

	if( serverList ) {
		serverList->SetOptions( showEmptyServers, showPlayerInfo );
	}
}