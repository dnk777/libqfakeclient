#ifndef LIBQFAKECLIENT_SYSTEM_H
#define LIBQFAKECLIENT_SYSTEM_H

#include "common.h"
#include "console.h"
#include "network_address.h"

#include <atomic>
#include <thread>
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

	std::thread::id pinnedToThreadId;

	System( Console *globalConsole );
	~System();

	void TimeFrame( unsigned maxMillis );
	void NetPollFrame( unsigned maxMillis );
	void ClientsFrame( unsigned maxMillis );

	void OnSocketReadable( ListenedSocket *listenedSocket );

public:
	/**
	 * Initializes the global System instance.
	 * This approach follows Quake conventions that require an explicit Init()/Shutdown() pair.
	 * It's safely to call the function from an arbitrary thread and do repeated calls.
	 * @param globalConsole A Console that will be used for the system instance.
	 */
	static void Init( Console *globalConsole );

	/**
	 * Shuts down the global system instance.
	 * This approach follows Quake conventions that require an explicit Init()/Shutdown() pair.
	 * It's safely to call the function from an arbitrary thread and do repeated calls.
	 */
	static void Shutdown();

	/**
	 * Gets the global System instance.
	 * There is an assumption that this call occurs between Init()/Shutdown() pair.
	 * Satisfying this condition is a burden of the caller.
	 * It's however safely to call the function from an arbitrary thread if the condition above holds.
	 */
	static System *Instance();

	inline Console *SystemConsole() { return console; }
	inline uint64_t Millis() { return millis; }
	void Sleep( unsigned millis );

	Socket *NewSocket( bool useIpV4 = true );
	void DeleteSocket( Socket *socket );

	/**
	 * Creates a new Client instance.
	 * It's safely to call the function from an arbitrary thread if calling System::Instance() is legal.
	 * @param console A Console the client will be using.
	 * @return A new Client instance, or null if a client creation is not possible.
	 */
	Client *NewClient( Console *console );

	/**
	 * Deletes the client instance.
	 * It's safely to call the function from an arbitrary thread if calling System::Instance() is legal.
	 * @param client A client to delete.
	 */
	void DeleteClient( Client *client );

	/**
	 * Adds a socket that gets tested for ingoing UDP messages in Frame() calls
	 * This call must be performed in the same thread the system is pinned to by a first Frame() call.
	 * @return True if the socket addition succeeded.
	 */
	bool AddListenedSocket( Socket *socket, void *owner, uint8_t *buffer, unsigned bufferSize,
							void ( *callback )( void *, const NetworkAddress &, unsigned ) );

	/**
	 * Removes a socket that was tested for ingoing UDP messages in Frame() calls.
	 * This call must be performed in the same thread the system is pinned to by a first Frame() call.
	 * @return True if the socket removal succeeded
	 */
	bool RemoveListenedSocket( Socket *socket );

	/**
	 * Runs the system and all attached clients.
	 * Note that the system becomes pinned to the current thread,
	 * and further attempts to modify it would lead to a failure.
	 * @param maxMillis A hint of how many millis should the system use.
	 */
	void Frame( unsigned maxMillis );

	/**
	 * Fails using abort() if the caller is not being executed in the thread
	 * the system is pinned to by a first Frame() call.
	 * There is an assumption that at least a single Frame() call has been executed first.
	 */
	void CheckThread( const char *function );
};

#endif
