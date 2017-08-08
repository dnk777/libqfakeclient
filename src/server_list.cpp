#include "command_parser.h"
#include "server_list.h"
#include "socket.h"

#include <inttypes.h>
#include <stdlib.h>

class AbstractPool
{
public:
	virtual void Free( PooledItem *item ) = 0;
};

template<typename T>
inline void Links<T>::Link( int prevPtrIndex, int nextPtrIndex, Links<T> **listHead ) {
	LinkForIndex( prevPtrIndex ) = nullptr;

	if( *listHead ) {
		( *listHead )->LinkForIndex( prevPtrIndex ) = this;
	}
	LinkForIndex( nextPtrIndex ) = *listHead;
	*listHead = this;
}

template<typename T>
inline void Links<T>::Unlink( int prevPtrIndex, int nextPtrIndex, Links<T> **listHead ) {
	if( auto *next = LinkForIndex( nextPtrIndex ) ) {
		next->LinkForIndex( prevPtrIndex ) = LinkForIndex( prevPtrIndex );
	}

	if( auto *prev = LinkForIndex( prevPtrIndex ) ) {
		prev->LinkForIndex( nextPtrIndex ) = LinkForIndex( nextPtrIndex );
	} else {
		assert( *listHead == this );
		*listHead = LinkForIndex( nextPtrIndex );
	}

	LinkForIndex( 0 ) = nullptr;
	LinkForIndex( 1 ) = nullptr;
}

template<typename T>
inline void Links<T>::LinkToHead( Links<T> **listHead ) {
	Link( 0, 1, listHead );
}

template<typename T>
inline void Links<T>::UnlinkFromHead( Links<T> **listHead ) {
	Unlink( 0, 1, listHead );
}

template<typename T>
inline void Links<T>::LinkToTail( Links<T> **listTail ) {
	Link( 1, 0, listTail );
}

template<typename T>
inline void Links<T>::UnlinkFromTail( Links<T> **listTail ) {
	Unlink( 1, 0, listTail );
}

inline void PooledItem::DeleteSelf() {
	this->~PooledItem();
	pool->Free( this );
}

// Assumes a default-constructible type
template <typename T, unsigned N>
class BasicPool : public AbstractPool
{
	Links<PooledItem> &LinksAt( unsigned index ) {
		return items[index].allocatorLinks;
	}

public:
	BasicPool( const BasicPool &that ) = delete;
	BasicPool &operator=( const BasicPool &that ) = delete;
	BasicPool( BasicPool &&that ) = delete;
	BasicPool &operator=( BasicPool &&that ) = delete;

	T items[N];
	Links<PooledItem> *freeItemLinks;
	unsigned count;

	BasicPool() {
		static_assert( N >= 2, "" );

		LinksAt( 0 ).PrevInList() = nullptr;
		LinksAt( 0 ).NextInList() = &LinksAt( 1 );
		LinksAt( 0 ).parent = &items[0];
		items[0].pool = this;

		for( unsigned i = 1; i < N - 1; ++i ) {
			LinksAt( i ).PrevInList() = &LinksAt( i - 1 );
			LinksAt( i ).NextInList() = &LinksAt( i + 1 );
			LinksAt( i ).parent = &items[i];
			items[i].pool = this;
		}
		LinksAt( N - 1 ).PrevInList() = &LinksAt( N - 2 );
		LinksAt( N + 1 ).NextInList() = nullptr;
		LinksAt( N + 1 ).parent = &items[N - 1];
		items[N - 1].pool = this;

		freeItemLinks = &LinksAt( 0 );
		count = 0;
	}

	T *Alloc() {
		if( freeItemLinks ) {
			T *result = (T *)freeItemLinks->Parent();
			result->allocatorLinks.UnlinkFromHead( &freeItemLinks );
			count++;
			return result;
		}
		return nullptr;
	}

	void Free( PooledItem *item ) override {
		count--;
		item->allocatorLinks.LinkToHead( &freeItemLinks );
	}
};

template<typename T, unsigned N>
class CompoundPool;

template <typename T, unsigned N>
class CompoundPoolChunk : public BasicPool<T, N>
{
	CompoundPool<T, N> *parent;

public:
	Links<CompoundPoolChunk<T, N> > chunkListLinks;
	CompoundPoolChunk( CompoundPool<T, N> *parent_ )
		: parent( parent_ ),
		chunkListLinks( this ) {};

	void Free( PooledItem *item ) override;
};

template <typename T, unsigned N>
class CompoundPool
{
	typedef CompoundPoolChunk<T, N> ChunkType;
	Links<ChunkType> *headChunk;
	unsigned limit;
	unsigned count;

	friend class CompoundPoolChunk<T, N>;

public:
	explicit CompoundPool( unsigned limit_ ) {
		headChunk = nullptr;
		limit = limit_;
		count = 0;
	}

	~CompoundPool() {
		MutableLinksIterator<ChunkType> iterator( &headChunk );

		while( iterator.HasNext() ) {
			ChunkType *chunk = iterator.Next();
			chunk->~ChunkType();
			free( chunk );
		}
	}

	T *Alloc() {
		if( count == limit ) {
			return nullptr;
		}

		MutableLinksIterator<ChunkType> iterator( &headChunk );

		while( iterator.HasNext() ) {
			ChunkType *chunk = iterator.Next();

			if( T *item = chunk->Alloc() ) {
				count++;
				return item;
			}
		}

		void *mem = malloc( sizeof( ChunkType ) );

		if( !mem ) {
			return nullptr;
		}
		auto *newChunk = new( mem )ChunkType( this );
		newChunk->chunkListLinks.LinkToHead( &headChunk );
		T *item = newChunk->Alloc();
		assert( item );
		count++;
		return item;
	}

	void Compactify() {
		MutableLinksIterator<ChunkType> iterator( &headChunk );

		while( iterator.HasNext() ) {
			ChunkType *chunk = iterator.Next();

			if( !chunk->count ) {
				iterator.Remove();
				chunk->chunkListLinks.UnlinkFromHead( &headChunk );
				chunk->~ChunkType();
				free( chunk );
			}
		}
	}
};

template<typename T, unsigned N>
void CompoundPoolChunk<T, N>::Free( PooledItem *item ) {
	BasicPool<T, N>::Free( item );
	// Make sure the parent count is modified too
	parent->count--;
}

typedef CompoundPool<ServerInfo, 32> ServerInfoPool;
typedef CompoundPool<PlayerInfo, 64> PlayerInfoPool;
typedef CompoundPool<PolledGameServer, 16> PolledGameServersPool;

static inline ServerInfoPool *AsServerInfoPool( void *address ) {
	return ( ServerInfoPool *)address;
}

static inline PlayerInfoPool *AsPlayerInfoPool( void *address ) {
	return ( PlayerInfoPool *)address;
}

static inline CompoundPool<PolledGameServer, 16> *AsPolledGameServersPool( void *address ) {
	return ( CompoundPool<PolledGameServer, 16> * )address;
}

class ServerInfoParser
{
	// Permanent fields, set once at initialization
	Message *message;
	Console *console;

	// These fields are used to pass info during parsing
	ServerInfo *info;
	const char *chars;
	uint64_t lastAcknowledgedChallenge;

	// This field is parsed along with info KV pairs
	uint64_t parsedChallenge;

	typedef bool ( ServerInfoParser::*Handler )( const char *, unsigned );

	struct HandlerEntry {
		HandlerEntry *nextInHashBin;
		Handler handler;
		const char *key;
		unsigned keyLength;
		uint32_t keyHash;

		HandlerEntry() {}
		HandlerEntry( const char *key_, Handler handler_ );

		bool CanHandle( const char *key_, unsigned keyLength_, uint32_t keyHash_ ) const {
			if( this->keyHash != keyHash_ || this->keyLength != keyLength_ ) {
				return false;
			}
			return !strncmp( this->key, key, keyLength_ );
		}

		bool Handle( ServerInfoParser *parser, const char *value, unsigned valueLength ) const {
			return ( parser->*handler )( value, valueLength );
		}
	};

	static constexpr auto HASH_MAP_SIZE = 17;
	HandlerEntry *handlersHashMap[HASH_MAP_SIZE];
	static constexpr auto MAX_HANDLERS = 16;
	HandlerEntry handlersStorage[MAX_HANDLERS];
	unsigned numHandlers;

	void AddHandler( const char *command, Handler handler );
	void LinkHandlerEntry( HandlerEntry *handlerEntry );

	bool HandleChallenge( const char *value, unsigned valueLength );
	bool HandleHostname( const char *value, unsigned valueLength );
	bool HandleMaxClients( const char *value, unsigned valueLength );
	bool HandleMapname( const char *value, unsigned valueLength );
	bool HandleMatchTime( const char *value, unsigned valueLength );
	bool HandleMatchScore( const char *value, unsigned valueLength );
	bool HandleGameFS(const char *value, unsigned valueLength);
	bool HandleGametype( const char *value, unsigned valueLength );
	bool HandleNumBots( const char *value, unsigned valueLength );
	bool HandleNumClients( const char *value, unsigned valueLength );
	bool HandleNeedPass( const char *value, unsigned valueLength );

	template<typename T>
	inline bool HandleInteger( const char *value, unsigned valueLength, T *result ) const;
	template<typename T, typename I>
	inline bool ParseInteger( const char *value, T *result, I ( *func )( const char *, char **, int ) ) const;

	template<uint8_t N>
	inline bool HandleString( const char *value, unsigned valueLength, BufferAndLength<N> *result ) const;

public:
	ServerInfoParser( Message *message_, Console *console_ );

	bool Parse( ServerInfo *info_, uint64_t lastAcknowledgedChallenge_ );
	bool HandleKVPair( unsigned keyStart, unsigned keyEnd, uint32_t keyHash, unsigned valueStart, unsigned valueEnd );

	inline uint64_t ParsedChallenge() const { return parsedChallenge; }
};

void ServerList::SocketCallback( void *owner, const NetworkAddress &address, unsigned dataSize ) {
	( (ServerList *) owner )->ParseIngoingData( address, dataSize );
}

void ServerList::ParseIngoingData( const NetworkAddress &address, unsigned dataSize ) {
	constexpr const char *function = "ServerList::ParseIngoingData()";

	if( dataSize < 5 ) {
		console->Printf( "%s: Warning: too few ingoing bytes\n", function );
		return;
	}

	message.Clear();
	message.currSize = dataSize;
	// Zero-terminate the message data for showing it as a string
	message.buffer[dataSize] = 0;

	int prefix;

	if( ( prefix = message.ReadLong() ) != -1 ) {
		console->Printf( "%s: Warning: bad ingoing data prefix: %d\n", function, prefix );
		return;
	}

	int byte;

	switch( ( byte = message.ReadByte() ) ) {
		case 'g':
		case 'G':
			ParseGetServersExtResponse( address );
			break;
		case 'i':
		case 'I':
			ParseInfoResponse( address );
			break;
		case 's':
		case 'S':
			ParseGetStatusResponse( address );
			break;
		default:
			console->Printf( "Unknown response prefix: %d\n", byte );
			break;
	}
}

void ServerList::ParseGetServersExtResponse( const NetworkAddress &address ) {
	constexpr const char *function = "ServerList::ParseGetServersExtResponse()";

	// TODO: Check whether the packet came from an actual master server
	// TODO: Is it possible at all? (We're talking about UDP packets).

	static const auto prefixLen = strlen( "getserversExtResponse" ) - 1;

	if( message.BytesLeft() <= prefixLen ) {
		console->Printf( "%s: Too few bytes in message for the expected prefix\n", function );
		return;
	}

	message.Skip( prefixLen );

	for(;; ) {
		if( !message.BytesLeft() ) {
			console->Printf( "%s: No bytes left in message\n", function );
			return;
		}
		const char startPrefix = (char)message.ReadByte();

		if( startPrefix == '\\' ) {
			if( message.BytesLeft() < 6 ) {
				console->Printf( "%s: Warning: Too few bytes in message for an IPv4 address\n", function );
				return;
			}
			uint8_t *addressBytes = message.Buffer() + message.ReadCount();
			uint8_t *portBytes = addressBytes + 4;

			// Stop parsing on a zero port. Its weird but that's what actual engine sources do.
			if( !( portBytes[0] | portBytes[1] ) ) {
				return;
			}
			OnServerIpV4AddressBytesReceived( addressBytes, portBytes );
			message.Skip( 6 );
		} else if( startPrefix == '/' ) {
			if( message.BytesLeft() < 18 ) {
				console->Printf( "%s: Warning: Too few bytes in message for an IPv6 address\n", function );
				return;
			}
			uint8_t *addressBytes = message.Buffer() + message.ReadCount();
			uint8_t *portBytes = addressBytes + 16;

			// Stop parsing on a zero port. Its weird but that's what actual engine sources do.
			if( !( portBytes[0] | portBytes[1] ) ) {
				return;
			}
			OnServerIpV6AddressBytesReceived( addressBytes, portBytes );
			message.Skip( 18 );
		} else {
			console->Printf( "%s: Warning: Illegal address prefix `%c`\n", function, startPrefix );
			return;
		}
	}
}

bool ServerList::ExpectPrefix( const char *prefix, size_t prefixLength, const char *caller ) {
	if( message.BytesLeft() <= prefixLength ) {
		console->Printf( "%s: Too few bytes in message for the expected prefix\n", caller );
		return false;
	}

	message.Skip( (unsigned)prefixLength );

	if( message.ReadByte() != '\n' ) {
		console->Printf( "%s: Expected a '\n' terminator of the prefix\n", caller );
		return false;
	}

	return true;
}

void ServerList::ParseInfoResponse( const NetworkAddress &address ) {
	constexpr const char *function = "ServerList::ParseInfoResponse()";

	PolledGameServer *server = FindServerByAddress( address );

	if( !server ) {
		// Be silent in this case, it can legally occur if a server times out and a packet arrives then
		return;
	}

	static const char *prefix = &"infoResponse"[1];
	static const size_t prefixLength = strlen( prefix );

	if( !ExpectPrefix( prefix, prefixLength, function ) ) {
		return;
	}

	ServerInfo *parsedServerInfo = ParseServerInfo( server );

	if( !parsedServerInfo ) {
		return;
	}

	if( message.BytesLeft() > 0 ) {
		console->Printf( "Warning: %s: there are extra bytes in the message\n", function );
		parsedServerInfo->DeleteSelf();
		return;
	}

	parsedServerInfo->hasPlayerInfo = false;
	OnNewServerInfo( server, parsedServerInfo );
}

ServerInfo *ServerList::ParseServerInfo( PolledGameServer *server ) {
	if( ServerInfo *info = AllocServerInfo() ) {
		if( serverInfoParser->Parse( info, server->lastAcknowledgedChallenge ) ) {
			server->lastAcknowledgedChallenge = serverInfoParser->ParsedChallenge();
			return info;
		}
		info->DeleteSelf();
	}

	return nullptr;
}

ServerInfoParser::ServerInfoParser( Message *message_, Console *console_ )
	: message( message_ ),
	console( console_ ),
	numHandlers( 0 ),
	info( nullptr ),
	chars( nullptr ),
	lastAcknowledgedChallenge( 0 ),
	parsedChallenge( 0 ) {
	memset( handlersHashMap, 0, sizeof( handlersHashMap ) );

	AddHandler( "challenge", &ServerInfoParser::HandleChallenge );
	AddHandler( "sv_hostname", &ServerInfoParser::HandleHostname );
	AddHandler( "sv_maxclients", &ServerInfoParser::HandleMaxClients );
	AddHandler( "mapname", &ServerInfoParser::HandleMapname );
	AddHandler( "g_match_time", &ServerInfoParser::HandleMatchTime );
	AddHandler( "g_match_score", &ServerInfoParser::HandleMatchScore );
	AddHandler( "fs_game", &ServerInfoParser::HandleGameFS );
	AddHandler( "gametype", &ServerInfoParser::HandleGametype );
	AddHandler( "bots", &ServerInfoParser::HandleNumBots );
	AddHandler( "clients", &ServerInfoParser::HandleNumClients );
	AddHandler( "g_needpass", &ServerInfoParser::HandleNeedPass );
}

bool ServerInfoParser::Parse( ServerInfo *info_, uint64_t lastAcknowledgedChallenge_ ) {
	this->info = info_;
	this->lastAcknowledgedChallenge = lastAcknowledgedChallenge_;
	this->parsedChallenge = 0;
	this->chars = (const char *)( message->Buffer() + message->ReadCount() );

	assert( message->CurrSize() >= message->ReadCount() );
	unsigned bytesLeft = message->CurrSize() - message->ReadCount();
	unsigned i = 0;

	constexpr const char *missingChallenge = "Warning: ServerList::ServerInfoParser::Parse(): missing a challenge\n";

	for(;; ) {
		if( i >= bytesLeft ) {
			if( !parsedChallenge ) {
				console->Printf( missingChallenge );
				return false;
			}
			message->SetReadCount( message->CurrSize() );
			return true;
		}

		// Expect new '\\'
		if( chars[i] != '\\' ) {
			return false;
		}
		i++;
		// Expect a key
		uint32_t keyHash = 0;
		unsigned keyStart = i;

		while( i < bytesLeft && chars[i] != '\\' ) {
			AddCharToHash( &keyHash, chars[i] );
			i++;
		}

		// If no '\\' has been found before end of data
		if( i >= bytesLeft ) {
			return false;
		}
		// Otherwise we have met a '\\'
		unsigned keyEnd = i;
		i++;
		// Expect a value
		unsigned valueStart = i;

		while( i < bytesLeft && chars[i] != '\\' && chars[i] != '\n' ) {
			i++;
		}

		unsigned valueEnd = i;

		if( !HandleKVPair( keyStart, keyEnd, keyHash, valueStart, valueEnd ) ) {
			return false;
		}

		if( chars[i] == '\n' ) {
			if( !parsedChallenge ) {
				console->Printf( missingChallenge );
				return false;
			}
			message->SetReadCount( message->ReadCount() + i );
			return true;
		}
	}
}

ServerInfoParser::HandlerEntry::HandlerEntry( const char *key_, Handler handler_ )
	: key( key_ ),
	handler( handler_ ),
	nextInHashBin( nullptr ) {
	this->keyHash = GetStringHashAndLength( key_, &this->keyLength );
}

void ServerInfoParser::AddHandler( const char *command, Handler handler ) {
	if( numHandlers < MAX_HANDLERS ) {
		void *mem = &handlersStorage[numHandlers++];
		LinkHandlerEntry( new( mem )HandlerEntry( command, handler ) );
		return;
	}
	console->Printf( "ServerList::ServerInfoParser::AddHandler(): too many handlers\n" );
	abort();
}

void ServerInfoParser::LinkHandlerEntry( HandlerEntry *handlerEntry ) {
	unsigned hashBinIndex = handlerEntry->keyHash % HASH_MAP_SIZE;

	handlerEntry->nextInHashBin = handlersHashMap[hashBinIndex];
	handlersHashMap[hashBinIndex] = handlerEntry;
}

bool ServerInfoParser::HandleKVPair( unsigned keyStart, unsigned keyEnd, uint32_t keyHash,
									 unsigned valueStart, unsigned valueEnd ) {
	assert( keyStart <= keyEnd );
	assert( valueStart <= valueEnd );

	const char *key = this->chars + keyStart;
	const char *value = this->chars + valueStart;
	unsigned keyLength = keyEnd - keyStart;
	unsigned valueLength = valueEnd - valueStart;

	unsigned hashBinIndex = keyHash % HASH_MAP_SIZE;

	for( HandlerEntry *entry = handlersHashMap[hashBinIndex]; entry; entry = entry->nextInHashBin ) {
		if( entry->CanHandle( key, keyLength, keyHash ) ) {
			return entry->Handle( this, value, valueLength );
		}
	}

	// If the key is unknown, return with success!
	return true;
}

template <typename T>
inline bool ServerInfoParser::HandleInteger( const char *value, unsigned valueLength, T *result ) const {
	if( sizeof( T ) > 4 ) {
		if( ( ( T )-1 ) != std::numeric_limits<T>::max() ) {
			return ParseInteger<T, long long>( value, result, strtoll );
		}
		return ParseInteger<T, unsigned long long>( value, result, strtoull );
	}

	if( ( ( T )-1 ) != std::numeric_limits<T>::max() ) {
		return ParseInteger<T, long>( value, result, strtol );
	}
	return ParseInteger<T, unsigned long>( value, result, strtoul );
}

template <typename T, typename I>
bool ServerInfoParser::ParseInteger( const char *value, T *result, I ( *func )( const char *, char **, int ) ) const {
	char *endptr;
	I parsed = func( value, &endptr, 10 );

	if( parsed == std::numeric_limits<I>::min() || parsed == std::numeric_limits<I>::max() ) {
		if( errno == ERANGE ) {
			return false;
		}
	}

	*result = (T)parsed;
	return true;
}

template<uint8_t N>
bool ServerInfoParser::HandleString( const char *value, unsigned valueLength, BufferAndLength<N> *result ) const {
	// Its better to pass a caller name but we do not really want adding extra parameters to this method
	constexpr const char *function = "ServerList::ServerInfoParser::HandleString()";

	if( valueLength > std::numeric_limits<uint8_t>::max() ) {
		console->Printf( "Warning: %s: the value `%s` exceeds result size limits\n", function, value );
		return false;
	}

	if( valueLength >= result->Capacity() ) {
		console->Printf( "Warning: %s: the value `%s` exceeds a result capacity %d\n", function, value, (int)result->Capacity() );
		return false;
	}

	result->SetFrom( value, valueLength );
	return true;
}

bool ServerInfoParser::HandleChallenge( const char *value, unsigned valueLength ) {
	if( !HandleInteger( value, valueLength, &parsedChallenge ) ) {
		return false;
	}
	return parsedChallenge > lastAcknowledgedChallenge;
}

bool ServerInfoParser::HandleHostname( const char *value, unsigned valueLength ) {
	return HandleString( value, valueLength, &info->serverName );
}

bool ServerInfoParser::HandleMaxClients( const char *value, unsigned valueLength ) {
	return HandleInteger( value, valueLength, &info->maxClients );
}

bool ServerInfoParser::HandleMapname( const char *value, unsigned valueLength ) {
	return HandleString( value, valueLength, &info->mapname );
}

static inline bool ScanInt( const char *s, char **endptr, int *result ) {
	long maybeResult = strtol( s, endptr, 10 );

	if( maybeResult == std::numeric_limits<long>::min() || maybeResult == std::numeric_limits<long>::max() ) {
		if( errno == ERANGE ) {
			return false;
		}
	}
	*result = (int)maybeResult;
	return true;
}

static inline bool ScanMinutesAndSeconds( const char *s, char **endptr, int *minutes, int8_t *seconds ) {
	int minutesValue, secondsValue;

	if( !ScanInt( s, endptr, &minutesValue ) ) {
		return false;
	}

	s = *endptr;

	if( *s != ':' ) {
		return false;
	}
	s++;

	if( !ScanInt( s, endptr, &secondsValue ) ) {
		return false;
	}

	if( minutesValue < 0 ) {
		return false;
	}

	if( secondsValue < 0 || secondsValue > 60 ) {
		return false;
	}
	*minutes = minutesValue;
	*seconds = (int8_t)secondsValue;
	return true;
}

#define DECLARE_MATCH_FUNC( funcName, flagString )            \
	static inline bool funcName( const char *s, char **endptr ) { \
		static const size_t length = strlen( flagString );        \
		if( !strncmp( s, flagString, length ) ) {                 \
			*endptr = const_cast<char *>( s + length );           \
			return true;                                          \
		}                                                         \
		return false;                                             \
	}

DECLARE_MATCH_FUNC( MatchOvertime, "overtime" )
DECLARE_MATCH_FUNC( MatchSuddenDeath, "suddendeath" )
DECLARE_MATCH_FUNC( MatchInTimeout, "(in timeout)" )

bool ServerInfoParser::HandleMatchTime( const char *value, unsigned valueLength ) {
	if( !strncmp( value, "Warmup", valueLength ) ) {
		info->time.isWarmup = true;
		return true;
	}

	if( !strncmp( value, "Finished", valueLength ) ) {
		info->time.isFinished = true;
		return true;
	}

	if( !strncmp( value, "Countdown", valueLength ) ) {
		info->time.isCountdown = true;
		return true;
	}

	char *ptr;

	if( !ScanMinutesAndSeconds( value, &ptr, &info->time.timeMinutes, &info->time.timeSeconds ) ) {
		return false;
	}

	if( ptr - value == valueLength ) {
		return true;
	}

	if( *ptr != ' ' ) {
		return false;
	}
	ptr++;

	if( *ptr == '/' ) {
		ptr++;

		if( *ptr != ' ' ) {
			return false;
		}
		ptr++;

		if( !ScanMinutesAndSeconds( value, &ptr, &info->time.limitMinutes, &info->time.limitSeconds ) ) {
			return false;
		}

		if( !*ptr ) {
			return true;
		}

		if( *ptr == ' ' ) {
			ptr++;
		}
	}

	for(;; ) {
		if( *ptr == 'o' && MatchOvertime( ptr, &ptr ) ) {
			info->time.isOvertime = true;
			continue;
		}

		if( *ptr == 's' && MatchSuddenDeath( ptr, &ptr ) ) {
			info->time.isSuddenDeath = true;
			continue;
		}

		if( *ptr == '(' && MatchInTimeout( ptr, &ptr ) ) {
			info->time.isTimeout = true;
			continue;
		}

		if( *ptr == ' ' ) {
			ptr++;
			continue;
		}

		if( *ptr == '/' || *ptr == '\n' ) {
			return true;
		}

		if( ptr - value >= valueLength ) {
			return false;
		}
	}
}

bool ServerInfoParser::HandleMatchScore( const char *value, unsigned valueLength ) {
	info->score.Clear();

	if( !valueLength ) {
		return true;
	}

	int scores[2] = { 0, 0 };
	unsigned offsets[2] = { 0, 0 };
	unsigned lengths[2] = { 0, 0 };
	const char *s = value;

	for( int i = 0; i < 2; ++i ) {
		while( *s == ' ' && ( s - value ) < valueLength ) {
			s++;
		}
		offsets[i] = (unsigned)( s - value );
		// Should not use strchr here (there is no zero terminator at the end of the value)
		while( *s != ':' && ( s - value ) < valueLength ) {
			s++;
		}

		if( ( s - value ) >= valueLength ) {
			return false;
		}
		lengths[i] = (unsigned)( s - value ) - offsets[i];

		if( lengths[i] >= info->score.scores[0].name.Capacity() ) {
			return false;
		}
		s++;

		if( *s != ' ' ) {
			return false;
		}
		s++;
		char *endptr;

		if( !ScanInt( s, &endptr, &scores[i] ) ) {
			return false;
		}
		s = endptr;
	}

	for( int i = 0; i < 2; ++i ) {
		auto *teamScore = &info->score.scores[i];
		teamScore->score = scores[i];
		teamScore->name.SetFrom( value + offsets[i], lengths[i] );
	}

	return true;
}

bool ServerInfoParser::HandleGameFS(const char *value, unsigned valueLength) {
	return HandleString( value, valueLength, &info->modname );
}

bool ServerInfoParser::HandleGametype( const char *value, unsigned valueLength ) {
	return HandleString( value, valueLength, &info->gametype );
}

bool ServerInfoParser::HandleNumBots( const char *value, unsigned valueLength ) {
	return HandleInteger( value, valueLength, &info->numBots );
}

bool ServerInfoParser::HandleNumClients( const char *value, unsigned valueLength ) {
	return HandleInteger( value, valueLength, &info->numClients );
}

bool ServerInfoParser::HandleNeedPass( const char *value, unsigned valueLength ) {
	return HandleInteger( value, valueLength, &info->needPassword );
}

void ServerList::ParseGetStatusResponse( const NetworkAddress &address ) {
	constexpr const char *function = "ServerList::ParseGetStatusResponse()";
	PolledGameServer *server = FindServerByAddress( address );

	if( !server ) {
		// Be silent in this case (see the explanation above).
		return;
	}

	static const char *prefix = &"statusResponse"[1];
	static const size_t prefixLength = strlen( prefix );

	if( !ExpectPrefix( prefix, prefixLength, function ) ) {
		return;
	}

	ServerInfo *parsedServerInfo = ParseServerInfo( server );

	if( !parsedServerInfo ) {
		return;
	}

	PlayerInfo *parsedPlayerInfo = nullptr;

	// ParsePlayerInfo() returns a null pointer if there is no clients.
	// Avoid qualifying this case as a parsing failure, do an actual parsing only if there are clients.
	if( parsedServerInfo->numClients ) {
		if( !( parsedPlayerInfo = ParsePlayerInfo() ) ) {
			parsedServerInfo->DeleteSelf();
			return;
		}
		assert( parsedPlayerInfo == parsedPlayerInfo->playersListLinks.Parent() );
		parsedServerInfo->playerInfoHead = &parsedPlayerInfo->playersListLinks;
	}

	parsedServerInfo->hasPlayerInfo = true;
	OnNewServerInfo( server, parsedServerInfo );
}

PlayerInfo *ServerList::ParsePlayerInfo() {
	Links<PlayerInfo> *listHead = nullptr;

	if( ParsePlayerInfo( &listHead ) ) {
		return listHead->Parent();
	}

	MutableLinksIterator<PlayerInfo> iterator( &listHead );

	while( iterator.HasNext() ) {
		PlayerInfo *info = iterator.Next();
		iterator.Remove();
		info->DeleteSelf();
	}

	return nullptr;
}

bool ServerList::ParsePlayerInfo( Links<PlayerInfo> **listHead ) {
	const char *chars = (const char *)( message.Buffer() + message.ReadCount() );

	assert( message.CurrSize() >= message.ReadCount() );
	unsigned bytesLeft = message.CurrSize() - message.ReadCount();
	const char *s = chars;

	Links<PlayerInfo> *listTail = nullptr;

	// Skip '\n' at the beginning (if any)
	if( *s == '\n' ) {
		s++;
	}

	int score, ping, team;
	char *endptr;

	for(;; ) {
		if( s - chars >= bytesLeft ) {
			break;
		}

		if( *s == '\n' ) {
			break;
		}

		if( !ScanInt( s, &endptr, &score ) ) {
			return false;
		}
		s = endptr + 1;

		if( s - chars >= bytesLeft ) {
			return false;
		}

		if( !ScanInt( s, &endptr, &ping ) ) {
			return false;
		}
		s = endptr + 1;

		if( s - chars >= bytesLeft ) {
			return false;
		}

		if( *s != '"' ) {
			return false;
		}
		s++;
		unsigned nameStart = (unsigned)( s - chars );
		unsigned nameLength = 0;

		for( ;; ) {
			if( s - chars >= bytesLeft ) {
				return false;
			}

			if( *s == '"' ) {
				nameLength = (unsigned)( s - chars ) - nameStart;
				break;
			}
			s++;
		}
		static_assert( sizeof( PlayerInfo::name ) < std::numeric_limits<uint8_t>::max(), "" );

		if( nameLength >= sizeof( PlayerInfo::name ) ) {
			return false;
		}
		s++;

		if( s - chars >= bytesLeft ) {
			return false;
		}

		if( !ScanInt( s, &endptr, &team ) ) {
			return false;
		}
		s = endptr;

		if( *s != '\n' ) {
			return false;
		}

		PlayerInfo *playerInfo = AllocPlayerInfo();

		if( !playerInfo ) {
			return false;
		}
		playerInfo->score = score;
		playerInfo->name.SetFrom( chars + nameStart, nameLength );
		playerInfo->ping = (uint16_t)ping;
		playerInfo->team = (uint8_t)team;

		if( !*listHead ) {
			*listHead = &playerInfo->playersListLinks;
		}
		playerInfo->playersListLinks.LinkToTail( &listTail );
		s++;
	}

	return true;
}

PolledGameServer *ServerList::FindServerByAddress( const NetworkAddress &address ) {
	const uint32_t hash = address.Hash();
	const unsigned hashBinIndex = hash % HASH_MAP_SIZE;

	MutableLinksIterator<PolledGameServer> iterator( &serversHashBins[hashBinIndex] );

	while( iterator.HasNext() ) {
		PolledGameServer *server = iterator.Next();

		if( server->addressHash != hash ) {
			continue;
		}

		if( server->networkAddress != address ) {
			continue;
		}
		return server;
	}
	return nullptr;
}

void ServerList::OnServerIpV4AddressBytesReceived( const uint8_t *addressBytes, const uint8_t *portBytes ) {
	const uint32_t hash = NetworkAddress::HashForIpV4Data( addressBytes, portBytes );
	const unsigned hashBinIndex = hash % HASH_MAP_SIZE;

	LinksIterator<PolledGameServer> iterator( serversHashBins[hashBinIndex] );

	while( iterator.HasNext() ) {
		const auto *server = iterator.Next();

		if( server->addressHash != hash ) {
			continue;
		}

		if( !server->networkAddress.MatchesIpV4Data( addressBytes, portBytes ) ) {
			continue;
		}
		return;
	}

	AddNewIpV4Server( addressBytes, portBytes, hash, hashBinIndex );
}

void ServerList::OnServerIpV6AddressBytesReceived( const uint8_t *addressBytes, const uint8_t *portBytes ) {
	const uint32_t hash = NetworkAddress::HashForIpV6Data( addressBytes, portBytes );
	const unsigned hashBinIndex = hash % HASH_MAP_SIZE;

	LinksIterator<PolledGameServer> iterator( serversHashBins[hashBinIndex] );

	while( iterator.HasNext() ) {
		const auto *server = iterator.Next();

		if( server->addressHash != hash ) {
			continue;
		}

		if( !server->networkAddress.MatchesIpV6Data( addressBytes, portBytes ) ) {
			continue;
		}
		return;
	}

	AddNewIpV6Server( addressBytes, portBytes, hash, hashBinIndex );
}

void ServerList::AddNewIpV4Server( const uint8_t *addressBytes, const uint8_t *portBytes,
								   uint32_t addressHash, unsigned hashBinIndex ) {
	if( auto *server = AllocPolledServer() ) {
		server->networkAddress.SetFromIpV4Data( addressBytes, portBytes );
		server->serversListLinks.LinkToHead( &serversHead );
		LinkServerToHashBin( server, addressHash, hashBinIndex );
	}
}

void ServerList::AddNewIpV6Server( const uint8_t *addressBytes, const uint8_t *portBytes,
								   uint32_t addressHash, unsigned hashBinIndex ) {
	if( auto *server = AllocPolledServer() ) {
		server->networkAddress.SetFromIpV6Data( addressBytes, portBytes );
		server->serversListLinks.LinkToHead( &serversHead );
		LinkServerToHashBin( server, addressHash, hashBinIndex );
	}
}

void ServerList::LinkServerToHashBin( PolledGameServer *server, const uint32_t addressHash, unsigned hashBinIndex ) {
	assert( !serversHashBins[hashBinIndex] || serversHashBins[hashBinIndex]->Parent() != server );
	server->addressHash = addressHash;
	server->hashBinIndex = hashBinIndex;
	server->hashBinLinks.LinkToHead( &serversHashBins[hashBinIndex] );
}

void ServerList::UnlinkServerFromHashBin( PolledGameServer *server ) {
	unsigned hashBinIndex = server->hashBinIndex;

	server->hashBinLinks.UnlinkFromHead( &serversHashBins[hashBinIndex] );
	assert( !serversHashBins[hashBinIndex] || serversHashBins[hashBinIndex]->Parent() != server );
	server->hashBinIndex = ~0u;
	server->addressHash = ~0u;
}

PolledGameServer::PolledGameServer()
	: serversListLinks( this ),
	hashBinLinks( this ),
	lastInfoRequestSentAt( 0 ),
	lastInfoReceivedAt( 0 ),
	currInfo( nullptr ),
	oldInfo( nullptr ),
	lastAcknowledgedChallenge( 0 ),
	instanceId( 0 ) {}

PolledGameServer *ServerList::AllocPolledServer() {
	auto *pool = AsPolledGameServersPool( this->polledServersPool );

	if( auto *mem = pool->Alloc() ) {
		auto *server = new(mem)PolledGameServer();
		server->instanceId = ++serverInstanceIdCounter;
		return server;
	}
	return nullptr;
}

ServerInfo::ServerInfo() {
	time.Clear();
	score.Clear();
	hasPlayerInfo = false;
	playerInfoHead = nullptr;
	maxClients = 0;
	numClients = 0;
	numBots = 0;
}

ServerInfo *ServerList::AllocServerInfo() {
	auto *pool = AsServerInfoPool( this->serverInfoPool );

	if( auto *mem = pool->Alloc() ) {
		return new(mem)ServerInfo();
	}
	return nullptr;
}

PlayerInfo *ServerList::AllocPlayerInfo() {
	auto *pool = AsPlayerInfoPool( this->playerInfoPool );

	if( auto *mem = pool->Alloc() ) {
		return new( mem )PlayerInfo();
	}
	return nullptr;
}

ServerList::ServerList( System *system_, Socket *ipV4Socket_, Socket *ipV6Socket_, int protocol_, ServerListListener *listener_ )
	: system( system_ ),
	console( system_->SystemConsole() ),
	ipV4Socket( ipV4Socket_ ),
	ipV6Socket( ipV6Socket_ ),
	serversHead( nullptr ),
	listener( listener_ ),
	protocol( protocol_ ),
	serverInstanceIdCounter( 0 ),
	polledServersPool( nullptr ), // Initialize these fields
	serverInfoPool( nullptr ),    // by null pointers
	playerInfoPool( nullptr ),    // to avoid an out-of-order initialization warning
	lastMasterServersPollAt( 0 ),
	lastMasterServerIndex( 0 ),
	showEmptyServers( false ),
	showPlayerInfo( false ) {
	memset( serversHashBins, 0, sizeof( serversHashBins ) );

	// Let it crash on segfaults...
	this->polledServersPool = new( malloc( sizeof( PolledGameServersPool ) ) )PolledGameServersPool( 256 );
	this->serverInfoPool = new( malloc( sizeof( ServerInfoPool ) ) )ServerInfoPool( 768 );
	this->playerInfoPool = new( malloc( sizeof( PlayerInfoPool ) ) )PlayerInfoPool( 2048 );

	void *parserMem = malloc( sizeof( ServerInfoParser ) );
	this->serverInfoParser = new( parserMem )ServerInfoParser( &message, system_->SystemConsole() );
}

ServerList::~ServerList() {
	listener->~ServerListListener();
	free( listener );

	this->serverInfoParser->~ServerInfoParser();
	free( this->serverInfoParser );

	AsPolledGameServersPool( this->polledServersPool )->~PolledGameServersPool();
	free( this->polledServersPool );

	AsServerInfoPool( this->serverInfoPool )->~ServerInfoPool();
	free( this->serverInfoPool );

	AsPlayerInfoPool( this->playerInfoPool )->~PlayerInfoPool();
	free( this->playerInfoPool );

	system->DeleteSocket( ipV4Socket );
	system->DeleteSocket( ipV6Socket );
}

void ServerList::Frame() {
	DropTimedOutServers();

	EmitPollMasterServersPackets();
	EmitPollGameServersPackets();

	//AsPolledGameServersPool( this->polledServersPool )->Compactify();
	//AsServerInfoPool( this->serverInfoPool )->Compactify();
	//AsPlayerInfoPool( this->playerInfoPool )->Compactify();
}

void ServerList::EmitPollMasterServersPackets() {
	const auto millisNow = system->Millis();

	if( millisNow - lastMasterServersPollAt < 750 ) {
		return;
	}

	// Make the warning affected by the timer too (do not spam in console way too often), do not return prematurely
	if( system->numMasterServers ) {
		lastMasterServerIndex = ( lastMasterServerIndex + 1 ) % system->numMasterServers;
		SendPollMasterServerPacket( system->masterServers[lastMasterServerIndex] );
	} else {
		console->Printf( "Warning: ServerList::EmitPollMasterServersPackets(): there are no master servers\n" );
	}

	lastMasterServersPollAt = millisNow;
}

void ServerList::EmitPollGameServersPackets() {
	const auto millisNow = system->Millis();

	MutableLinksIterator<PolledGameServer> iterator( &serversHead );

	while( iterator.HasNext() ) {
		PolledGameServer *server = iterator.Next();

		if( millisNow - server->lastInfoRequestSentAt < 300 ) {
			continue;
		}
		SendPollGameServerPacket( server );
		server->lastInfoRequestSentAt = millisNow;
	}
}

void ServerList::DropTimedOutServers() {
	const auto millisNow = system->Millis();

	MutableLinksIterator<PolledGameServer> iterator( &serversHead );

	while( iterator.HasNext() ) {
		PolledGameServer *server = iterator.Next();

		if( millisNow - server->lastInfoRequestSentAt < 1000 ) {
			// Wait for the first info received...
			if( server->lastInfoReceivedAt && millisNow - server->lastInfoReceivedAt > 5000 ) {
				iterator.Remove();
				DropServer( server );
			}
		}
	}
}

void ServerList::DropServer( PolledGameServer *server ) {
	listener->OnServerRemoved( *server );
	UnlinkServerFromHashBin( server );
	server->DeleteSelf();
}

void ServerList::SendPollMasterServerPacket( const NetworkAddress &address ) {
	if( !SendPacket( address, "getserversExt Warsow %d full%s", protocol, showEmptyServers ? " empty" : "" ) ) {
		console->Printf( "Warning: ServerList::SendPollMasterServerPacket() failure\n" );
	}
}

void ServerList::SendPollGameServerPacket( PolledGameServer *server ) {
	uint64_t challenge = system->Millis();
	bool result;

	if( showPlayerInfo ) {
		result = SendPacket( server->networkAddress, "getstatus %" PRIu64, challenge );
	} else {
		result = SendPacket( server->networkAddress, "getinfo %" PRIu64, challenge );
	}

	if( !result ) {
		console->Printf( "Warning: ServerList::SendPollGameServerPacket() failure\n" );
		return;
	}
}

inline Socket *ServerList::SocketForAddressKind( const NetworkAddress &address ) {
	if( address.IsIpV4Address() ) {
		return ipV4Socket;
	}

	if( address.IsIpV6Address() ) {
		return ipV6Socket;
	}
	abort();
}

bool ServerList::SendPacket( const NetworkAddress &address, const char *format, ... ) {
	message.Clear();
	message.WriteLong( ~0 );

	assert( message.CurrSize() == 4 );
	char *bufferChars = ( char * )( message.Buffer() + message.CurrSize() );
	size_t bufferSize = message.MaxSize() - message.CurrSize();

	va_list va;
	va_start( va, format );
	int bytesPrinted = vsnprintf( bufferChars, bufferSize, format, va );
	va_end( va );

	if( bytesPrinted < 0 || bytesPrinted >= bufferSize ) {
		return false;
	}

	// Send the last zero byte not counted in bytesPrinted too
	return SocketForAddressKind( address )->SendDatagram( address, message.Buffer(), (unsigned)( 4 + bytesPrinted + 1 ) );
}

void ServerList::OnNewServerInfo( PolledGameServer *server, ServerInfo *newServerInfo ) {
	if( server->oldInfo ) {
		server->oldInfo->DeleteSelf();
		assert( server->currInfo );
		server->oldInfo = server->currInfo;
	}
	server->oldInfo = server->currInfo;
	server->currInfo = newServerInfo;
	server->lastInfoReceivedAt = system->Millis();

	if( !newServerInfo->MatchesOld( server->oldInfo ) ) {
		if( server->oldInfo ) {
			listener->OnServerUpdated( *server );
		} else {
			// Defer server addition until a first info arrives.
			// Otherwise there is just nothing to show in a server browser.
			// If there is no old info, the listener has not been notified about a new server yet.
			listener->OnServerAdded( *server );
		}
	}
}

void MatchTime::Clear() {
	memset( this, 0, sizeof( MatchTime ) );
}

bool MatchTime::operator==( const MatchTime &that ) const {
	return !memcmp( this, &that, sizeof( MatchTime ) );
}

void MatchScore::Clear() {
	scores[0].Clear();
	scores[1].Clear();
}

bool MatchScore::operator==( const MatchScore &that ) const {
	// Its better to do integer comparisons first, thats why there are no individual TeamScore::Equals() methods
	for( int i = 0; i < 2; ++i ) {
		if( this->scores[i].score != that.scores[i].score ) {
			return false;
		}
	}

	for( int i = 0; i < 2; ++i ) {
		if( this->scores[i].name.actualLength != that.scores[i].name.actualLength ) {
			return false;
		}
	}

	for( int i = 0; i < 2; ++i ) {
		if( memcmp( this->scores[i].name.chars, that.scores[i].name.chars, this->scores[i].name.actualLength ) ) {
			return false;
		}
	}
	return true;
}

bool PlayerInfo::operator==( const PlayerInfo &that ) const {
	// Do these cheap comparisons first
	if( this->score != that.score || this->ping != that.ping || this->team != that.team ) {
		return false;
	}
	return this->name == that.name;
}

ServerInfo::~ServerInfo() {
	MutableLinksIterator<PlayerInfo> iterator( &playerInfoHead );

	while( iterator.HasNext() ) {
		PlayerInfo *info = iterator.Next();
		iterator.Remove();
		info->DeleteSelf();
	}
	playerInfoHead = nullptr;
}

PolledGameServer::~PolledGameServer() {
	if( oldInfo ) {
		oldInfo->DeleteSelf();
		oldInfo = nullptr;
	}

	if( currInfo ) {
		currInfo->DeleteSelf();
		currInfo = nullptr;
	}
}

bool ServerInfo::MatchesOld( ServerInfo *oldInfo ) {
	if( !oldInfo ) {
		return false;
	}

	// Test fields that are likely to change often first

	if( this->time != oldInfo->time ) {
		return false;
	}

	if( this->numClients != oldInfo->numClients ) {
		return false;
	}

	if( this->hasPlayerInfo && oldInfo->hasPlayerInfo ) {
		LinksIterator<PlayerInfo> thisInfoIterator( this->playerInfoHead );
		LinksIterator<PlayerInfo> thatInfoIterator( oldInfo->playerInfoHead );

		for(;; ) {
			if( !thisInfoIterator.HasNext() ) {
				if( !thatInfoIterator.HasNext() ) {
					break;
				}
				return false;
			}

			if( !thatInfoIterator.HasNext() ) {
				return false;
			}
			const PlayerInfo *thisInfo = thisInfoIterator.Next();
			const PlayerInfo *thatInfo = thatInfoIterator.Next();

			if( *thisInfo != *thatInfo ) {
				return false;
			}
		}
	} else if( this->hasPlayerInfo != oldInfo->hasPlayerInfo ) {
		return false;
	}

	if( this->score != oldInfo->score ) {
		return false;
	}

	if( mapname != oldInfo->mapname ) {
		return false;
	}

	if( gametype != oldInfo->gametype ) {
		return false;
	}

	if( this->numBots != oldInfo->numBots ) {
		return false;
	}

	// Never changes until server restart

	if( serverName != oldInfo->serverName ) {
		return false;
	}

	if( modname != oldInfo->modname ) {
		return false;
	}

	return this->maxClients == oldInfo->maxClients && this->needPassword == oldInfo->needPassword;
}