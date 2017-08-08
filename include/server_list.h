#ifndef LIBQFAKECLIENT_SERVER_LIST_H
#define LIBQFAKECLIENT_SERVER_LIST_H

#include "network_address.h"
#include "channel.h"

class AbstractPool;

/**
 * An abstract item that has intrusive prev/next links,
 * can link self to a list head or tail and unlink self from ones.
 */
template<typename T>
class Links
{
	inline void Link( int prevPtrIndex, int nextPtrIndex, Links **head );
	inline void Unlink( int prevPtrIndex, int nextPtrIndex, Links **head );

	Links *&LinkForIndex( int index ) {
		return prevAndNextInList[index];
	}

public:
	Links() : parent( nullptr ) {}
	explicit Links( T *parent_ ) : parent( parent_ ) {}

	T *Parent() { return parent; }
	const T *Parent() const { return parent; }

	T *parent;
	Links *prevAndNextInList[2];

	Links *&PrevInList() { return prevAndNextInList[0]; }
	Links *&NextInList() { return prevAndNextInList[1]; }

	const Links *PrevInList() const { return prevAndNextInList[0]; }
	const Links *NextInList() const { return prevAndNextInList[1]; }

	inline void LinkToHead( Links **listHead );
	inline void UnlinkFromHead( Links **listHead );
	inline void LinkToTail( Links **listTail );
	inline void UnlinkFromTail( Links **listTail );
};

template<typename T>
class LinksIterator
{
	const Links<T> *curr;

public:
	LinksIterator( const Links<T> *head ) : curr( head ) {}

	bool HasNext() const { return curr != nullptr; }

	const T *Next() {
		const T *result = this->curr->Parent();

		assert( result );
		this->curr = this->curr->NextInList();
		return result;
	}
};

template<typename T>
class MutableLinksIterator
{
	Links<T> *curr;
	Links<T> **head;
	Links<T> *lastRetrieved;

public:
	MutableLinksIterator( Links<T> **head ) {
		this->curr = *head;
		this->head = head;
		this->lastRetrieved = nullptr;
	}

	bool HasNext() const { return curr != nullptr; }

	T *Next() {
		T *result = this->curr->Parent();

		assert( result );
		this->lastRetrieved = this->curr;
		this->curr = this->curr->NextInList();
		return result;
	}

	/**
	 * Removes the last retrieved item from the list.
	 * This call assumes that the Next() call has been called before it.
	 * A repeated Remove() call without Next() calls in-between fails.
	 */
	void Remove() {
		assert( this->lastRetrieved );
		this->lastRetrieved->UnlinkFromHead( this->head );
		this->lastRetrieved = nullptr;
	}
};

class PooledItem
{
public:
	Links<PooledItem> allocatorLinks;
	AbstractPool *pool;

	PooledItem() : allocatorLinks( this ) {}

	virtual ~PooledItem() {}
	inline void DeleteSelf();
};

template<uint8_t N>
struct BufferAndLength {
	char chars[N];
	uint8_t actualLength;

	BufferAndLength() {
		Clear();
	}

	void Clear() {
		chars[0] = '\0';
		actualLength = 0;
	}

	static uint8_t Capacity() {
		static_assert( N > 0, "Illegal chars buffer size" );
		return (uint8_t)( N - 1 );
	}

	const char *Get( uint8_t *length = nullptr ) const {
		if( length ) {
			*length = actualLength;
		}
		return chars;
	}

	template<uint8_t M>
	bool operator==( const BufferAndLength<M> &that ) const {
		if( this->actualLength != that.actualLength ) {
			return false;
		}
		return !memcmp( this->chars, that.chars, this->actualLength );
	}

	template<uint8_t M>
	bool operator!=( const BufferAndLength<M> &that ) const {
		return !( *this == that );
	}

	void SetFrom( const char *chars_, unsigned numChars ) {
		assert( numChars < N );
		memcpy( this->chars, chars_, numChars );
		this->chars[numChars] = '\0';
		this->actualLength = (uint8_t)numChars;
	}
};

class PlayerInfo : public PooledItem
{
public:
	Links<PlayerInfo> playersListLinks;
	int score;
	BufferAndLength<32> name;
	uint16_t ping;
	uint8_t team;

	PlayerInfo()
		: playersListLinks( this ), score( 0 ), ping( 0 ), team( 0 ) {}

	bool operator==( const PlayerInfo &that ) const;
	bool operator!=( const PlayerInfo &that ) const {
		return !( *this == that );
	}
};

struct MatchTime {
	int timeMinutes;
	int limitMinutes;
	int8_t timeSeconds;
	int8_t limitSeconds;
	bool isWarmup : 1;
	bool isCountdown : 1;
	bool isFinished : 1;
	bool isOvertime : 1;
	bool isSuddenDeath : 1;
	bool isTimeout : 1;

	void Clear();
	bool operator==( const MatchTime &that ) const;
	bool operator!=( const MatchTime &that ) const {
		return !( *this == that );
	}
};

struct MatchScore {
	struct TeamScore {
		int score;
		BufferAndLength<32> name;

		void Clear() {
			score = 0;
			name.Clear();
		}
	};

	TeamScore scores[2];

	const TeamScore &AlphaScore() const { return scores[0]; }
	const TeamScore &BetaScore() const { return scores[1]; }

	void Clear();
	bool operator==( const MatchScore &that ) const;
	bool operator!=( const MatchScore &that ) const {
		return ( *this == that );
	}
};

class ServerInfo : public PooledItem
{
public:
	BufferAndLength<64> serverName;
	BufferAndLength<32> gametype;
	BufferAndLength<32> modname;
	BufferAndLength<32> mapname;

	ServerInfo();
	~ServerInfo() override;

	// May be null even if extended player info is present
	Links<PlayerInfo> *playerInfoHead;

	MatchTime time;
	MatchScore score;

	uint8_t maxClients;
	uint8_t numClients;
	uint8_t numBots;

	bool needPassword;

	// Indicates if an extended player info is present.
	bool hasPlayerInfo;

	bool MatchesOld( ServerInfo *oldInfo );
};

class PolledGameServer : public PooledItem
{
	friend class ServerList;

	Links<PolledGameServer> serversListLinks;
	Links<PolledGameServer> hashBinLinks;

	uint32_t addressHash;
	unsigned hashBinIndex;
	NetworkAddress networkAddress;

	ServerInfo *currInfo;
	ServerInfo *oldInfo;

	int64_t lastInfoRequestSentAt;
	int64_t lastInfoReceivedAt;

	uint64_t lastAcknowledgedChallenge;

	unsigned instanceId;

	inline const ServerInfo *CheckInfo() const {
		assert( currInfo );
		return currInfo;
	}

public:
	PolledGameServer();
	~PolledGameServer() override;

	inline const ServerInfo *OldInfo() const { return oldInfo; }
	inline const ServerInfo *CurrInfo() const { return currInfo; }

	inline unsigned InstanceId() const { return instanceId; }

	inline const NetworkAddress &Address() const { return networkAddress; }

	inline const BufferAndLength<64> &ServerName() const {
		return CheckInfo()->serverName;
	}

	inline const BufferAndLength<32> &ModName() const {
		return CheckInfo()->modname;
	}

	inline const BufferAndLength<32> &Gametype() const {
		return CheckInfo()->gametype;
	}

	inline const BufferAndLength<32> &MapName() const {
		return CheckInfo()->mapname;
	}

	inline const MatchTime &Time() const { return CheckInfo()->time; }
	inline const MatchScore &Score() const { return CheckInfo()->score; }

	inline uint8_t MaxClients() const { return CheckInfo()->maxClients; }
	inline uint8_t NumClients() const { return CheckInfo()->numClients; }
	inline uint8_t NumBots() const { return CheckInfo()->numBots; }
	inline bool HasPlayerInfo() const { return CheckInfo()->hasPlayerInfo; }
	inline bool NeedPassword() const { return CheckInfo()->needPassword; }

	LinksIterator<PlayerInfo> PlayerInfoIterator() const {
		return LinksIterator<PlayerInfo>( CheckInfo()->playerInfoHead );
	}
};

class ServerListListener
{
public:
	virtual ~ServerListListener() {}

	virtual void OnServerAdded( const PolledGameServer &server ) = 0;
	virtual void OnServerRemoved( const PolledGameServer &server ) = 0;
	virtual void OnServerUpdated( const PolledGameServer &server ) = 0;
};

class System;
class Socket;

class ServerInfoParser;

class ServerList
{
	Message message;

	System *system;
	Socket *ipV4Socket;
	Socket *ipV6Socket;
	Console *console;

	ServerListListener *listener;

	Links<PolledGameServer> *serversHead;

	void *serverInfoPool;
	void *playerInfoPool;
	void *polledServersPool;

	static constexpr unsigned HASH_MAP_SIZE = 97;
	Links<PolledGameServer> *serversHashBins[HASH_MAP_SIZE];

	int64_t lastMasterServersPollAt;
	unsigned lastMasterServerIndex;

	unsigned serverInstanceIdCounter;
	int protocol;

	bool showEmptyServers;
	bool showPlayerInfo;

	void ParseIngoingData( const NetworkAddress &address, unsigned dataSize );

	void ParseGetServersExtResponse( const NetworkAddress &address );
	void OnServerIpV4AddressBytesReceived( const uint8_t *addressBytes, const uint8_t *portBytes );
	void OnServerIpV6AddressBytesReceived( const uint8_t *addressBytes, const uint8_t *portBytes );
	void AddNewIpV4Server( const uint8_t *addressBytes, const uint8_t *portBytes,
						   uint32_t addressHash, unsigned hashBinIndex );
	void AddNewIpV6Server( const uint8_t *addressBytes, const uint8_t *portBytes,
						   uint32_t addressHash, unsigned hashBinIndex );
	void LinkServerToHashBin( PolledGameServer *server, uint32_t addressHash, unsigned hashBinIndex );
	void UnlinkServerFromHashBin( PolledGameServer *server );

	inline ServerInfo *AllocServerInfo();
	inline PlayerInfo *AllocPlayerInfo();
	inline PolledGameServer *AllocPolledServer();

	bool ExpectPrefix( const char *prefix, size_t prefixLength, const char *caller );
	void ParseInfoResponse( const NetworkAddress &address );
	void ParseGetStatusResponse( const NetworkAddress &address );

	void OnNewServerInfo( PolledGameServer *server, ServerInfo *parsedServerInfo );

	ServerInfoParser *serverInfoParser;

	ServerInfo *ParseServerInfo( PolledGameServer *server );
	PlayerInfo *ParsePlayerInfo();
	bool ParsePlayerInfo( Links<PlayerInfo> **listHead );

	PolledGameServer *FindServerByAddress( const NetworkAddress &address );

	void EmitPollMasterServersPackets();
	void SendPollMasterServerPacket( const NetworkAddress &address );
	void EmitPollGameServersPackets();
	void SendPollGameServerPacket( PolledGameServer *server );

	inline Socket *SocketForAddressKind( const NetworkAddress &address );

#ifndef _MSC_VER
	bool SendPacket( const NetworkAddress &address, const char *format, ... ) __attribute__( ( format( printf, 3, 4 ) ) );
#else
	bool SendPacket( const NetworkAddress &address, _Printf_format_string_ const char *format, ... );
#endif

	void DropTimedOutServers();
	void DropServer( PolledGameServer *server );

public:
	ServerList( System *system_, Socket *ipV4Socket_, Socket *ipV6Socket_, int protocol_, ServerListListener *listener_ );
	~ServerList();

	void SetOptions( bool showEmptyServers_, bool showPlayerInfo_ ) {
		this->showEmptyServers = showEmptyServers_;
		this->showPlayerInfo = showPlayerInfo_;
	}

	static void SocketCallback( void *owner, const NetworkAddress &address, unsigned dataSize );
	inline uint8_t *SocketBuffer() { return message.Buffer(); }
	inline unsigned BufferSize() { return message.MaxSize(); }

	void Frame();
};

#endif
