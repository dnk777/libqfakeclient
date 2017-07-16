#include "channel.h"
#include "client.h"
#include "command_parser.h"
#include "common.h"
#include "console.h"
#include "message_parser.h"

#include <initializer_list>
#include <new>
#include <limits>
#include <inttypes.h>
#include <stdlib.h>

struct Constants21 {
	static constexpr auto PROTOCOL = PROTOCOL21;
	static constexpr auto MAX_CONFIGSTRINGS = 4256;

	static constexpr auto PS_MAX_STATS = 64;
	static constexpr auto MAX_PM_STATS = 16;
	static constexpr auto MAX_GAME_STATS = 16;
	static constexpr auto MAX_GAME_LONGSTATS = 8;
	static constexpr auto MAX_ITEMS = 64;

	static constexpr auto STAT_TEAM = 9;

	static constexpr auto SV_BITFLAGS_RELIABLE = 1 << 1;
	static constexpr auto SV_BITFLAGS_HTTP = 1 << 3;
	static constexpr auto SV_BITFLAGS_BASEURL = 1 << 4;

	enum {
		SVC_BAD,

		SVC_NOP,
		SVC_SERVERCMD,
		SVC_SERVERDATA,
		SVC_SPAWNBASELINE,
		SVC_DOWNLOAD,
		SVC_PLAYERINFO,
		SVC_PACKETENTITIES,
		SVC_GAMECOMMANDS,
		SVC_MATCH,
		SVC_CLACK,
		SVC_SERVERCS,
		SVC_FRAME,
		SVC_DEMOINFO,
		SVC_EXTENSION
	};
};

class ClientWorldState21 : public ClientWorldState, Constants21
{
	friend class MessageParser21;

public:
	char downloadUrlBuffer[MAX_STRING_CHARS + 1];

	char motdBuffer[MAX_STRING_CHARS + 1];
	char gameBuffer[MAX_STRING_CHARS + 1];
	char levelBuffer[MAX_STRING_CHARS + 1];

	short statsBuffer[MAX_SERVER_CLIENTS][PS_MAX_STATS];
	char configStringsBuffer[MAX_CONFIGSTRINGS][MAX_CONFIGSTRING_CHARS];

	ClientWorldState21() {
		ClientWorldState::motd = motdBuffer;
		ClientWorldState::game = gameBuffer;
		ClientWorldState::stats = &statsBuffer[0][0];
		ClientWorldState::configStrings = &configStringsBuffer[0][0];
		ClientWorldState::configStringsStride = MAX_CONFIGSTRING_CHARS;
		ClientWorldState::maxConfigStrings = MAX_CONFIGSTRINGS;

		downloadUrlBuffer[0] = 0;
		motdBuffer[0] = 0;
		gameBuffer[0] = 0;
		levelBuffer[0] = 0;
	}

	void Clear() override;

	bool IsConnectionReliable() const override {
		return ( bitFlags & SV_BITFLAGS_RELIABLE ) != 0;
	}

	~ClientWorldState21() {}
};

void ClientWorldState21::Clear() {
	ClientWorldState::Clear();

	stats = &statsBuffer[0][0];
	statsStride = PS_MAX_STATS;

	configStrings = &configStringsBuffer[0][0];
	configStringsStride = MAX_CONFIGSTRING_CHARS;
	maxConfigStrings = MAX_CONFIGSTRINGS;

	downloadUrl = downloadUrlBuffer;
	downloadUrlBuffer[0] = 0;

	motd = motdBuffer;
	motdBuffer[0] = 0;

	game = gameBuffer;
	gameBuffer[0] = 0;

	level = levelBuffer;
	levelBuffer[0] = 0;
}

struct ConsolePtr {
	Console *console;

	ConsolePtr( Console *console_ ) : console( console_ ) {}

	void Printf( const char *format, ... ) {
		if( !console ) {
			return;
		}

		va_list va;
		va_start( va, format );
		console->VPrintf( format, va );
		va_end( va );
	}

	void VPrintf( const char *format, va_list va ) {
		if( !console ) {
			return;
		}

		console->VPrintf( format, va );
	}
};

void ClientWorldState::Clear() {
	protocol = 0;
	playerNum = 0;
	spawnCount = 0;
	downloadPort = 0;
	downloadUrl = nullptr;
	motd = nullptr;
	game = nullptr;
	level = nullptr;
	stats = nullptr;
	statsStride = 0;
	configStrings = nullptr;
	configStringsStride = 0;
	maxConfigStrings = 0;
}

ClientWorldState *ClientWorldState::New( int protocolVersion, Console *debugConsole ) {
	if( protocolVersion != Constants21::PROTOCOL ) {
		ConsolePtr( debugConsole ).Printf( "Only 2.1 protocol is supported at this moment\n" );
		return nullptr;
	}

	void *mem = malloc( sizeof( ClientWorldState21 ) );

	if( !mem ) {
		ConsolePtr( debugConsole ).Printf( "Cannot allocate memory for a ClientWorldState\n" );
		return nullptr;
	}

	return new(mem)ClientWorldState21();
}

void ClientWorldState::Delete( ClientWorldState *worldState ) {
	if( worldState ) {
		worldState->~ClientWorldState();
		free( worldState );
	}
}

class MessageParser21 : public MessageParser, Constants21
{
	static constexpr auto PS_M_TYPE = 1 << 0;
	static constexpr auto PS_M_ORIGIN0 = 1 << 1;
	static constexpr auto PS_M_ORIGIN1 = 1 << 2;
	static constexpr auto PS_M_ORIGIN2 = 1 << 3;
	static constexpr auto PS_M_VELOCITY0 = 1 << 4;
	static constexpr auto PS_M_VELOCITY1 = 1 << 5;
	static constexpr auto PS_M_VELOCITY2 = 1 << 6;

	static constexpr auto PS_MOREBITS1 = 1 << 7;

	static constexpr auto PS_M_TIME = 1 << 8;
	static constexpr auto PS_EVENT = 1 << 9;
	static constexpr auto PS_EVENT2 = 1 << 10;
	static constexpr auto PS_WEAPONSTATE = 1 << 11;
	static constexpr auto PS_INVENTORY = 1 << 12;
	static constexpr auto PS_FOV = 1 << 13;
	static constexpr auto PS_VIEWANGLES = 1 << 14;

	static constexpr auto PS_MOREBITS2 = 1 << 15;

	static constexpr auto PS_POVNUM = 1 << 16;
	static constexpr auto PS_VIEWHEIGHT = 1 << 17;
	static constexpr auto PS_PMOVESTATS = 1 << 18;
	static constexpr auto PS_M_FLAGS = 1 << 19;
	static constexpr auto PS_PLRKEYS = 1 << 20;

	static constexpr auto PS_MOREBITS3 = 1 << 23;

	static constexpr auto PS_M_GRAVITY = 1 << 24;
	static constexpr auto PS_M_DELTA_ANGLES0 = 1 << 25;
	static constexpr auto PS_M_DELTA_ANGLES1 = 1 << 26;
	static constexpr auto PS_M_DELTA_ANGLES2 = 1 << 27;
	static constexpr auto PS_PLAYERNUM = 1 << 28;

	static constexpr auto EV_INVERSE = 128;

	static constexpr auto SNAP_INVENTORY_LONGS = ( ( MAX_ITEMS + 31 ) / 32 );
	static constexpr auto SNAP_STATS_LONGS = ( ( PS_MAX_STATS + 31 ) / 32 );

	static constexpr auto PM_STAT_SIZE = 16;

	static constexpr auto FRAMESNAP_FLAG_DELTA = ( 1 << 0 );
	static constexpr auto FRAMESNAP_FLAG_MULTIPOV = ( 1 << 2 );

	static constexpr auto U_ORIGIN1 = 1 << 0;
	static constexpr auto U_ORIGIN2 = 1 << 1;
	static constexpr auto U_ORIGIN3 = 1 << 2;

	static constexpr auto U_ANGLE1 = 1 << 3;
	static constexpr auto U_ANGLE2 = 1 << 4;

	static constexpr auto U_EVENT = 1 << 5;
	static constexpr auto U_REMOVE = 1 << 6;

	static constexpr auto U_MOREBITS1 = 1 << 7;

	static constexpr auto U_NUMBER16 = 1 << 8;
	static constexpr auto U_FRAME8 = 1 << 9;
	static constexpr auto U_SVFLAGS = 1 << 10;
	static constexpr auto U_MODEL = 1 << 11;
	static constexpr auto U_TYPE = 1 << 12;
	static constexpr auto U_OTHERORIGIN = 1 << 13;
	static constexpr auto U_SKIN8 = 1 << 14;

	static constexpr auto U_MOREBITS2 = 1 << 15;

	static constexpr auto U_EFFECTS8 = 1 << 16;
	static constexpr auto U_WEAPON = 1 << 17;
	static constexpr auto U_SOUND = 1 << 18;
	static constexpr auto U_MODEL2 = 1 << 19;
	static constexpr auto U_LIGHT = 1 << 20;
	static constexpr auto U_SOLID = 1 << 21;
	static constexpr auto U_EVENT2 = 1 << 22;

	static constexpr auto U_MOREBITS3 = 1 << 23;

	static constexpr auto U_SKIN16 = 1 << 24;
	static constexpr auto U_ANGLE3 = 1 << 25;
	static constexpr auto U_ATTENUATION = 1 << 26;
	static constexpr auto U_EFFECTS16 = 1 << 27;
	static constexpr auto U_FRAME16 = 1 << 29;
	static constexpr auto U_TEAM = 1 << 30;

	static constexpr auto SOLID_BMODEL = 31;

	static constexpr auto ET_INVERSE = 128;

	~MessageParser21() {}

	Message initialMessage;
	ClientWorldState21 *worldState;

	int lastExecutedServerCmdNum;
	int lastCmdAck;
	int playerNums[MAX_SERVER_CLIENTS];

	void Reset() {
		serverTime = 0;
		lastFrame = -1;
		lastExecutedServerCmdNum = 0;
		lastCmdAck = -1;
		// TODO: Only currSize is set to zero in the original code
		initialMessage.Clear();

		for( int i = 0; i < MAX_SERVER_CLIENTS; ++i ) {
			playerNums[i] = i;
		}
	}

	void ParseDemoInfo( Message &message );
	void ParseClientAck( Message &message );
	void ParseServerCmd( Message &message );
	void ParseServerCs( Message &message );
	void ParseServerData( Message &message );
	void ParseSpawnBaseLine( Message &message );
	void ParseFrame( Message &message );

	void ParseFrameHeader( Message &message, int *length, uint64_t *serverTime, int *frame, int *flags );
	void ParseGameCommands( Message &message, int frame, int flags );
	void ParseAreaBits( Message &message );
	void ParseDeltaGameState( Message &message );
	void ParsePlayerStates( Message &message );
	void ParsePlayerState( Message &message, short *oldStats, int index );
	void ParsePacketEntities( Message &message, unsigned startPos, int snapshotLength );

	void SetStat( int player, int index, short value );

	unsigned ReadEntityBits( Message &message );
	void ReadDeltaEntity( Message &message );
	void ReadDummyOrigin( Message &message );
	void ReadDummyCoord( Message &message );
	void ReadDummyAngle( Message &message );
	void ReadDummyAngle16( Message &message );

public:
	MessageParser21( Console *console_, Client *client_, ClientWorldState21 *worldState_ )
		: MessageParser( console_, worldState_, client_ ), worldState( worldState_ ) {
		Reset();
	}

	void Parse( Message &message ) override;
};

GenericClientProtocolExecutor *MessageParser::Executor() {
	return client->protocolExecutor;
}

MessageParser *MessageParser::New( int protocolVersion, Client *client, ClientWorldState *worldState, Console *console, Console *debugConsole ) {
	if( protocolVersion != Constants21::PROTOCOL ) {
		ConsolePtr( debugConsole ).Printf( "Only 2.1 protocol is supported at this moment\n" );
		return nullptr;
	}

	ClientWorldState21 *state21 = dynamic_cast<ClientWorldState21 *>( worldState );

	if( !state21 ) {
		ConsolePtr( debugConsole ).Printf( "Illegal client world state (should match 2.1 protocol)\n" );
		return nullptr;
	}

	void *mem = malloc( sizeof( MessageParser21 ) );

	if( !mem ) {
		ConsolePtr( debugConsole ).Printf( "Cannot allocate memory for a MessageParser\n" );
		return nullptr;
	}

	return new(mem)MessageParser21( console, client, state21 );
}

void MessageParser::Delete( MessageParser *parser ) {
	if( parser ) {
		parser->~MessageParser();
		free( parser );
	}
}

void MessageParser21::Parse( Message &message ) {
	for(;; ) {
		if( !message.BytesLeft() ) {
			return;
		}
		const int cmdPrefix = message.ReadByte();

		switch( cmdPrefix ) {
			case SVC_DEMOINFO:
				ParseDemoInfo( message );
				break;
			case SVC_CLACK:
				ParseClientAck( message );
				break;
			case SVC_SERVERCMD:
				ParseServerCmd( message );
				break;
			case SVC_SERVERCS:
				ParseServerCs( message );
				break;
			case SVC_SERVERDATA:
				ParseServerData( message );
				break;
			case SVC_SPAWNBASELINE:
				ParseSpawnBaseLine( message );
				break;
			case SVC_FRAME:
				ParseFrame( message );
				break;
			default:
				console->Printf( "Unknown server command prefix %d\n", cmdPrefix );
				abort();
		}
	}
}

void MessageParser21::ParseDemoInfo( Message &message ) {
	message.ReadLong();
	message.ReadLong();
	ssize_t metaDataRealSize = message.ReadLong();
	ssize_t metaDataMaxSize = message.ReadLong();
	ssize_t end = message.ReadCount() + metaDataRealSize;

	while( message.ReadCount() < end ) {
		// We have to make a copy of it... message.ReadString() overwrites its buffer on a repeated call
		char key[MAX_MSG_STRING_CHARS + 1];
		strcpy( key, message.ReadString() );
		console->Printf( "Demo info: %s %s\n", key, message.ReadString() );
	}
	ssize_t bytesToSkip = metaDataMaxSize - metaDataRealSize + end - message.ReadCount();

	if( bytesToSkip > 0 ) {
		message.Skip( (unsigned) bytesToSkip );
	}
}

void MessageParser21::ParseClientAck( Message &message ) {
	const int ack = message.ReadLong();

	if( ack > this->lastCmdAck ) {
		Executor()->TryAcknowledge( ack );
		this->lastCmdAck = ack;
	}
	message.ReadLong();
	Executor()->Activate();
}

void MessageParser21::ParseServerCmd( Message &message ) {
	if( !worldState->IsConnectionReliable() ) {
		int cmdNum = message.ReadLong();

		if( cmdNum <= lastExecutedServerCmdNum ) {
			// Skip the command and return
			message.ReadString();
			return;
		}
		lastExecutedServerCmdNum = cmdNum;
		Executor()->SendCommandAck( cmdNum );
	}

	// Fallthrough
	ParseServerCs( message );
}

void MessageParser21::ParseServerCs( Message &message ) {
	Executor()->ExecuteCommandFromServer( message.ReadString() );
}

void MessageParser21::ParseServerData( Message &message ) {
	worldState->protocol = message.ReadLong();
	worldState->spawnCount = message.ReadLong();
	message.ReadShort(); // snap frametime
	message.ReadString(); // base game

	const char *game = message.ReadString();
	strncpy( worldState->gameBuffer, game, sizeof( worldState->gameBuffer ) );
	worldState->gameBuffer[sizeof( worldState->gameBuffer ) - 1] = 0;

	worldState->playerNum = message.ReadShort() + 1;

	const char *level = message.ReadString();
	strncpy( worldState->levelBuffer, level, sizeof( worldState->levelBuffer ) );
	worldState->gameBuffer[sizeof( worldState->levelBuffer ) - 1] = 0;

	int bitFlags = message.ReadByte();
	worldState->bitFlags = bitFlags;

	if( bitFlags & SV_BITFLAGS_HTTP ) {
		// Read either URL or port for downloads
		if( bitFlags & SV_BITFLAGS_BASEURL ) {
			message.ReadString();
		} else {
			message.ReadShort();
		}
	}

	// For each pure pak read (actually skip) its name and checksum
	for( int i = 0, pureNum = message.ReadShort(); i < pureNum; ++i ) {
		message.ReadString();
		message.ReadLong();
	}
}

void MessageParser21::ParseSpawnBaseLine( Message &message ) {
	ReadDeltaEntity( message );
}

void MessageParser21::ParseFrameHeader( Message &message, int *length, uint64_t *serverTime, int *frame, int *flags ) {
	*length = message.ReadShort();

	// Note: should read a 64-bit integer in 2.1+
	*serverTime = (uint64_t)message.ReadLong();
	*frame = message.ReadLong();

	message.ReadLong(); // delta frame number
	message.ReadLong(); // ucmd executed

	*flags = (uint8_t)message.ReadByte();
	message.ReadByte(); // suppressCount
}

void MessageParser21::ParseGameCommands( Message &message, int frame, int flags ) {
	int prefix = (uint8_t)message.ReadByte();

	if( prefix != SVC_GAMECOMMANDS ) {
		console->Printf( "MessageParser21::ParseGameCommands(): Expected SVC_GAMECOMMANDS, got %d\n", prefix );
		abort();
	}

	int8_t targets[MAX_SERVER_CLIENTS / 8];

	for(;; ) {
		int framediff = message.ReadShort();

		if( framediff == -1 ) {
			break;
		}
		const char *cmd = message.ReadString();
		int numTargets = 0;

		if( flags & FRAMESNAP_FLAG_MULTIPOV ) {
			memset( targets, 0, sizeof( targets ) );
			numTargets = message.ReadByte();
			message.ReadData( targets, (unsigned) numTargets );
		}

		if( frame > this->lastFrame + framediff ) {
			if( !numTargets ) {
				Executor()->ExecuteCommandFromServer( cmd );
			} else {
				console->Printf( "Multiple targets are not supported\n" );
				abort();
			}
		}
	}
}

void MessageParser21::ParsePlayerStates( Message &message ) {
	short *oldStats = worldState->stats;

	int players = 0;
	int prefix;

	while( ( prefix = message.ReadByte() ) != 0 ) {
		if( prefix != SVC_PLAYERINFO ) {
			console->Printf( "MessageParser21::ParsePlayerStates(): expected SVC_PLAYERINFO, got %d\n", prefix );
			abort();
		}
		ParsePlayerState( message, oldStats + playerNums[players + 1] * PS_MAX_STATS, players );
		players++;
	}

	while( players < MAX_SERVER_CLIENTS - 1 ) {
		SetStat( playerNums[players + 1], STAT_TEAM, 0 );
		players++;
	}
}

void MessageParser21::ParseAreaBits( Message &message ) {
	unsigned numBytes = (uint8_t)message.ReadByte();

	message.Skip( numBytes );
}

void MessageParser21::ParsePacketEntities( Message &message, unsigned startPos, int snapshotLength ) {
	int prefix = (uint8_t)message.ReadByte();

	if( prefix != SVC_PACKETENTITIES ) {
		console->Printf( "MessageParser21::ParsePacketEntities(): expected SVC_PACKETENTITIES, got %d\n", prefix );
		abort();
	}

	unsigned bytesRead = message.ReadCount() - startPos;
	int snapshotBytesLeft = snapshotLength - bytesRead;

	if( snapshotBytesLeft > 0 ) {
		message.Skip( (unsigned)snapshotBytesLeft );
	}
}

void MessageParser21::ParseFrame( Message &message ) {
	int length, frame, flags;

	if( message.BytesLeft() < 2 ) {
		console->Printf( "Can't read snapshot length\n" );
		abort();
	}

	unsigned startPos = message.ReadCount() + 2;
	ParseFrameHeader( message, &length, &this->serverTime, &frame, &flags );
	ParseGameCommands( message, frame, flags );
	ParseAreaBits( message );
	ParseDeltaGameState( message );
	ParsePlayerStates( message );
	ParsePacketEntities( message, startPos, length );

	if( frame > this->lastFrame ) {
		Executor()->SendFrameAck( frame, serverTime );
	}

	this->lastFrame = frame;
}

void MessageParser21::ParseDeltaGameState( Message &message ) {
	int prefix = (uint8_t)message.ReadByte();

	if( prefix != SVC_MATCH ) {
		console->Printf( "Expected SVC_MATCH, got %d\n", prefix );
		abort();
	}

	uint8_t longStatBits = (uint8_t)message.ReadByte();
	short statBits = (short)message.ReadShort();

	static_assert( MAX_GAME_LONGSTATS == 8, "" );
	static_assert( MAX_GAME_STATS == 16, "" );

	if( longStatBits ) {
		for( int i = 0, mask = 1; i < MAX_GAME_LONGSTATS; ++i, mask <<= 1 ) {
			if( longStatBits & mask ) {
				message.ReadLong();
			}
		}
	}

	if( statBits ) {
		for( int i = 0, mask = 1; i < MAX_GAME_STATS; ++i, mask <<= 1 ) {
			if( statBits & mask ) {
				message.ReadShort();
			}
		}
	}
}

void MessageParser21::SetStat( int player, int index, short value ) {
	worldState->statsBuffer[player][index] = value;
}

unsigned MessageParser21::ReadEntityBits( Message &message ) {
	unsigned result = (uint8_t)message.ReadByte();

	if( result & U_MOREBITS1 ) {
		result &= ~U_MOREBITS1;
		unsigned byte = (uint8_t)message.ReadByte();
		result |= ( byte << 8 ) & 0x0000FF00u;
	}

	if( result & U_MOREBITS2 ) {
		result &= ~U_MOREBITS2;
		unsigned byte = (uint8_t)message.ReadByte();
		result |= ( byte << 16 ) & 0x00FF0000u;
	}

	if( result & U_MOREBITS3 ) {
		result &= ~U_MOREBITS3;
		unsigned byte = (uint8_t)message.ReadByte();
		result |= ( byte << 24 ) & 0xFF000000u;
	}

	// Read (skip) entity num
	if( result & U_NUMBER16 ) {
		result &= ~U_NUMBER16;
		message.ReadShort();
	} else {
		message.ReadByte();
	}

	return result;
}

void MessageParser21::ReadDeltaEntity( Message &message ) {
	unsigned bits = ReadEntityBits( message );
	int solid = 0;

	if( bits & U_TYPE ) {
		bits &= ~U_TYPE;
		message.ReadByte();
	}

	if( bits & U_SOLID ) {
		bits &= ~U_SOLID;
		solid = message.ReadShort();
	}

	for( auto modelBits: { U_MODEL, U_MODEL2 } ) {
		if( bits & modelBits ) {
			bits &= ~modelBits;
			message.ReadShort();
		}
	}

	if( bits & U_FRAME8 ) {
		bits &= ~U_FRAME8;
		message.ReadByte();
	}

	if( bits & U_FRAME16 ) {
		bits &= ~U_FRAME16;
		message.ReadShort();
	}

	if( ( bits & U_SKIN8 ) && ( bits & U_SKIN16 ) ) {
		bits &= ~( U_SKIN8 | U_SKIN16 );
		message.ReadLong();
	} else if( bits & U_SKIN8 ) {
		bits &= ~U_SKIN8;
		message.ReadByte();
	} else if( bits & U_SKIN16 ) {
		bits &= ~U_SKIN16;
		message.ReadShort();
	}

	if( ( bits & U_EFFECTS8 ) && ( bits & U_EFFECTS16 ) ) {
		bits &= ~( U_EFFECTS8 | U_EFFECTS16 );
		message.ReadLong();
	} else if( bits & U_EFFECTS16 ) {
		bits &= ~U_EFFECTS16;
		message.ReadShort();
	} else if( bits & U_EFFECTS8 ) {
		bits &= ~U_EFFECTS8;
		message.ReadByte();
	}

	// Since the read coords are skipped anyway, we have fused a separate branch for a linear projectile
	for( auto originBits: { U_ORIGIN1, U_ORIGIN2, U_ORIGIN3 } ) {
		if( originBits & bits ) {
			bits &= ~originBits;
			ReadDummyCoord( message );
		}
	}

	for( auto angleBits: { U_ANGLE1, U_ANGLE2, U_ANGLE3 } ) {
		if( bits & angleBits ) {
			bits &= ~angleBits;

			if( solid != SOLID_BMODEL ) {
				ReadDummyAngle( message );
			} else {
				ReadDummyAngle16( message );
			}
		}
	}

	if( bits & U_OTHERORIGIN ) {
		bits &= ~U_OTHERORIGIN;
		ReadDummyOrigin( message );
	}

	if( bits & U_SOUND ) {
		bits &= ~U_SOUND;
		message.ReadShort();
	}

	for( auto eventBits: { U_EVENT, U_EVENT2 } ) {
		if( bits & eventBits ) {
			bits &= ~eventBits;

			if( ( (uint8_t)message.ReadByte() ) & ET_INVERSE ) {
				message.ReadByte();
			}
		}
	}

	if( bits & U_ATTENUATION ) {
		bits &= ~U_ATTENUATION;
		message.ReadByte();
	}

	if( bits & U_WEAPON ) {
		bits &= ~U_WEAPON;
		message.ReadByte();
	}

	if( bits & U_SVFLAGS ) {
		bits &= ~U_SVFLAGS;
		message.ReadShort();
	}

	if( bits & U_LIGHT ) {
		bits &= ~U_LIGHT;
		message.ReadLong();
	}

	if( bits & U_TEAM ) {
		bits &= ~U_TEAM;
		message.ReadByte();
	}

	assert( !bits );
}

void MessageParser21::ReadDummyOrigin( Message &message ) {
	for( int i = 0; i < 3; ++i ) {
		message.ReadInt3();
	}
}

void MessageParser21::ReadDummyCoord( Message &message ) {
	message.ReadInt3();
}

void MessageParser21::ReadDummyAngle( Message &message ) {
	message.ReadByte();
}

void MessageParser21::ReadDummyAngle16( Message &message ) {
	message.ReadShort();
}

void MessageParser21::ParsePlayerState( Message &message, short *oldStats, int index ) {
	int flags = (uint8_t)message.ReadByte();
	unsigned byte;

	if( flags & PS_MOREBITS1 ) {
		byte = (uint8_t)message.ReadByte();
		flags |= ( byte << 8 );
	}

	if( flags & PS_MOREBITS2 ) {
		byte = (uint8_t)message.ReadByte();
		flags |= ( byte << 16 );
	}

	if( flags & PS_MOREBITS3 ) {
		byte = (uint8_t)message.ReadByte();
		flags |= ( byte << 24 );
	}

	if( flags & PS_M_TYPE ) {
		message.ReadByte();
	}

	for( auto originBit: { PS_M_ORIGIN0, PS_M_ORIGIN1, PS_M_ORIGIN2 } ) {
		if( flags & originBit ) {
			message.ReadInt3();
		}
	}

	for( auto velocityBit: { PS_M_VELOCITY0, PS_M_VELOCITY1, PS_M_VELOCITY2 } ) {
		if( flags & velocityBit ) {
			message.ReadInt3();
		}
	}

	if( flags & PS_M_TIME ) {
		message.ReadByte();
	}

	if( flags & PS_M_FLAGS ) {
		message.ReadShort();
	}

	for( auto angleBit: { PS_M_DELTA_ANGLES0, PS_M_DELTA_ANGLES1, PS_M_DELTA_ANGLES2 } ) {
		if( flags & angleBit ) {
			message.ReadShort();
		}
	}

	for( auto eventFlags: { PS_EVENT, PS_EVENT2 } ) {
		if( flags & eventFlags ) {
			if( ( (uint8_t)message.ReadByte() ) & EV_INVERSE ) {
				message.ReadByte();
			}
		}
	}

	if( flags & PS_VIEWANGLES ) {
		for( int i = 0; i < 3; ++i ) {
			message.ReadShort();
		}
	}

	if( flags & PS_M_GRAVITY ) {
		message.ReadShort();
	}

	if( flags & PS_WEAPONSTATE ) {
		message.ReadByte();
	}

	if( flags & PS_FOV ) {
		message.ReadByte();
	}

	if( flags & PS_POVNUM ) {
		message.ReadByte();
	}

	if( flags & PS_PLAYERNUM ) {
		playerNums[index] = message.ReadByte();
	}

	if( flags & PS_VIEWHEIGHT ) {
		message.ReadChar();
	}

	if( flags & PS_PMOVESTATS ) {
		flags &= ~PS_PMOVESTATS;
		// Prevent sign extension
		int bits = (uint16_t)message.ReadShort();

		for( int i = 0, mask = 1; i < PM_STAT_SIZE; ++i, mask <<= 1 ) {
			if( bits & mask ) {
				message.ReadShort();
			}
		}
	}

	if( flags & PS_INVENTORY ) {
		flags &= ~PS_INVENTORY;
		int inventoryStatBits[SNAP_INVENTORY_LONGS];

		for( int i = 0; i < SNAP_INVENTORY_LONGS; ++i ) {
			inventoryStatBits[i] = message.ReadLong();
		}

		for( int i = 0; i < MAX_ITEMS; ++i ) {
			if( inventoryStatBits[i >> 5] & ( 1 << ( i & 31 ) ) ) {
				message.ReadByte();
			}
		}
	}

	if( flags & PS_PLRKEYS ) {
		flags &= ~PS_PLRKEYS;
		message.ReadByte();
	}

	int statBits[SNAP_STATS_LONGS];

	for( int i = 0; i < SNAP_INVENTORY_LONGS; ++i ) {
		statBits[i] = message.ReadLong();
	}

	for( int i = 0; i < PS_MAX_STATS; ++i ) {
		if( statBits[i >> 5] & ( 1 << ( i & 31 ) ) ) {
			SetStat( playerNums[i + 1], i, (short)message.ReadShort() );
		} else {
			SetStat( playerNums[i + 1], i, oldStats[i] );
		}
	}
}
