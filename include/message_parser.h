#ifndef LIBQFAKECLIENT_MESSAGE_PARSER_H
#define LIBQFAKECLIENT_MESSAGE_PARSER_H

#include "protocol_executor.h"

class Console;
class Client;
class GenericClientProtocolExecutor;
class Message;

class ClientWorldState
{
public:
	int protocol;
	int playerNum;
	int spawnCount;
	int bitFlags;

	uint16_t downloadPort;
	const char *downloadUrl;

	const char *motd;
	const char *game;
	const char *level;

	short *stats;
	unsigned statsStride;

	char *configStrings;
	unsigned configStringsStride;
	unsigned maxConfigStrings;

	ClientWorldState()
		: protocol( 0 ),
		playerNum( 0 ),
		spawnCount( 0 ),
		downloadPort( 0 ),
		downloadUrl( nullptr ),
		motd( nullptr ),
		game( nullptr ),
		level( nullptr ),
		stats( nullptr ),
		configStrings( nullptr ) {}

	virtual ~ClientWorldState() {};

	virtual bool IsConnectionReliable() const = 0;

	virtual void Clear();

	char *ConfigStringsData() { return configStrings; }
	unsigned ConfigStringsStride() const { return configStringsStride; }
	unsigned MaxConfigStrings() const { return maxConfigStrings; }

	int PlayerNum() const { return playerNum; }
	int SpawnCount() const { return spawnCount; }

	static ClientWorldState *New( int protocolVersion, Console *debugConsole = nullptr );
	static void Delete( ClientWorldState *worldState );
};

class MessageParser
{
	friend class GenericClientProtocolExecutor;
	// Might and should be hidden by a world state subtype in a derived class, that's why it is private.
	ClientWorldState *worldState;

protected:
	Console *console;
	// Its obvious that we should use a GenericClientProtocolExecutor reference here,
	// but it introduces an initialization dependency loop. Use client->executor instead.
	Client *client;

	GenericClientProtocolExecutor *Executor();

	int64_t lastFrame;
	uint64_t serverTime;

public:
	MessageParser( Console *console_, ClientWorldState *worldState_, Client *client_ )
		: console( console_ ), worldState( worldState_ ), client( client_ ) {}

	virtual ~MessageParser() {}

	static MessageParser *New( int protocolVersion, Client *client, ClientWorldState *worldState, Console *console, Console *debugConsole = nullptr );
	static void Delete( MessageParser *parser );

	virtual void Parse( Message &message ) = 0;
};

#endif
