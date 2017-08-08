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

class ServerList;
class ServerListListener;

class System
{
	friend class ServerList;

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

	static constexpr auto MAX_SOCKETS = MAX_FAKE_CLIENT_INSTANCES + 2;

	ListenedSocket listenedSockets[MAX_SOCKETS];
	unsigned numListenedSockets;

	static constexpr unsigned MAX_MASTER_SERVERS = 4;
	NetworkAddress masterServers[MAX_MASTER_SERVERS];
	unsigned numMasterServers;

	ServerList *serverList;
	bool pendingShowEmptyServersOption;
	bool pendingShowPlayerInfoOption;

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
	 * Adds a master server address that might be used in server list updates.
	 * It's safe to call the function from an arbitrary thread if calling System::Instance() is legal.
	 * @return True if the addition succeeded,
	 *         false if a maximum master servers count has been reached or the server is already present.
	 */
	bool AddMasterServer( const NetworkAddress &address );

	/**
	 * Removes a master server address. The address might no longer be used in server list updates.
	 * It's safe to call the function from an arbitrary thread if calling System::Instance() is legal.
	 * @return True if the removal succeeded (there was such server address), false otherwise.
	 */
	bool RemoveMasterServer( const NetworkAddress &address );

	/**
	 * Checks whether an address is known as a master server address.
	 * It's safe to call the function from an arbitrary thread if calling System::Instance() is legal.
	 */
	bool IsMasterServer( const NetworkAddress &address ) const;

	/**
	 * Starts polling master and game servers for actual server status.
	 * Note that this call is not idempotent.
	 * A duplicated call without StopServerListUpdates() in-between leads to an abortion.
	 * It's safe to call the function from an arbitrary thread if calling System::Instance() is legal.
	 * @param listener A {@link ServerListListener} that gets notified about server status updates. Must not be null.
	 * @return True if start of the polling succeeded.
	 */
	bool StartUpdatingServerList( ServerListListener *listener );

	/**
	 * Sets several options that affect server status output transferred via {@link ServerListListener}.
	 * Note that a prior StartUpdatingServerList() call is not mandatory.
	 * Application of these settings will be deferred in this case to an actual start of updating the server list.
	 * It's safe to call the function from an arbitrary thread if calling System::Instance() is legal.
	 * @param showEmpty Whether empty servers should be shown.
	 * @param showPlayerInfo Whether list of players should be queried in addition to overall server parameters.
	 */
	void SetServerListUpdateOptions( bool showEmpty, bool showPlayerInfo );

	/**
	 * Stops updating the server list.
	 * This call is idempotent and is allowed to be called without a prior StartUpdatingServerList() call.
	 * It's safe to call the function from an arbitrary thread if calling System::Instance() is legal.
	 */
	void StopUpdatingServerList();

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
	 * Skips the current thread testing if the system is not pinned to a thread yet.
	 */
	void CheckThread( const char *function );
};

#endif
