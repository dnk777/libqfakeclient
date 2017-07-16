#ifndef LIBQFAKECLIENT_SYSTEM_H
#define LIBQFAKECLIENT_SYSTEM_H

#include "common.h"
#include "console.h"
#include "network_address.h"

#include <stdint.h>

class Socket;
class Client;

class System
{
	Console *console;

	uint64_t millis;
	void *timestamp;

	Client *clients[MAX_FAKE_CLIENT_INSTANCES];

	struct ListenedSocket {
		Socket *socket;
		void *owner;
		uint8_t *buffer;
		unsigned bufferSize;
		void (*callback)( void *, const NetworkAddress &, unsigned );

		void RunCallback( const NetworkAddress &address, unsigned dataSize ) {
			callback( owner, address, dataSize );
		}
	};

	static constexpr auto MAX_SOCKETS = MAX_FAKE_CLIENT_INSTANCES + 1;

	ListenedSocket listenedSockets[MAX_SOCKETS];
	unsigned numListenedSockets;

	static System *globalSystem;

	System( Console *globalConsole );
	~System();

	void TimeFrame( unsigned maxMillis );
	void NetPollFrame( unsigned maxMillis );
	void ClientsFrame( unsigned maxMillis );

	void OnSocketReadable( ListenedSocket *listenedSocket );

public:
	static void Init( Console *globalConsole );
	static void Shutdown();
	static inline System *Instance() { return globalSystem; }

	inline Console *SystemConsole() { return console; }
	inline uint64_t Millis() { return millis; }
	void Sleep( unsigned millis );

	Socket *NewSocket( bool useIpV4 = true );
	void DeleteSocket( Socket *socket );

	Client *NewClient( Console *console );
	void DeleteClient( Client *client );

	bool AddListenedSocket( Socket *socket, void *owner, uint8_t *buffer, unsigned bufferSize,
							void ( *callback )( void *, const NetworkAddress &, unsigned ) );
	bool RemoveListenedSocket( Socket *socket );

	void Frame( unsigned maxMillis );
};

#endif
