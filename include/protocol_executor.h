#ifndef LIBQFAKECLIENT_PROTOCOL_EXECUTOR_H
#define LIBQFAKECLIENT_PROTOCOL_EXECUTOR_H

#include <limits>
#include "network_address.h"
#include "command_buffer.h"

class Client;
class ClientWorldState;
class CommandParser;
class Console;
class MessageParser;
class System;

class AbstractClientProtocolExecutor
{
	friend class Client;
	friend class CommandHandlersRegistry;

protected:
	bool autoReconnect;

	Client *client;
	Console *console;
	System *system;

	char *configStringsData;
	unsigned maxConfigStrings;
	unsigned configStringsStride;

	char *ConfigString( unsigned index ) {
		if( index >= maxConfigStrings ) {
			return nullptr;
		}

		return configStringsData + index * configStringsStride;
	}

public:
	AbstractClientProtocolExecutor( Client *client );

	virtual ~AbstractClientProtocolExecutor() {};

	virtual void Reset() = 0;

	virtual void Frame() = 0;

	virtual void ExecuteCommandFromClient( const char *command ) = 0;
};

class GenericClientProtocolExecutor;

class CommandHandlersRegistry
{
public:
	typedef void (GenericClientProtocolExecutor::*CommandHandler)( CommandParser & );

private:
	GenericClientProtocolExecutor *executor;
	const char *tag;

	static constexpr auto MAX_HANDLERS = 48;
	static_assert( MAX_HANDLERS <= std::numeric_limits<int8_t>::max(), "Cannot use a int8_t for an entry index\n" );

	// We use indices instead of pointers to save some memory.
	// Negative indices are meant to be treated as "null" ones.

	struct HashEntry {
		const char *name;
		CommandHandler handler;
		uint32_t tag;
		uint32_t nameHash;
		uint8_t nameLength;
		int8_t prevInHashBin;
		int8_t nextInHashBin;
		int8_t nextInFreeList;
		int8_t nextInUsedList;
		int8_t prevInUsedList;
	};

	HashEntry entriesData[MAX_HANDLERS];

	int8_t firstFreeEntry;
	int8_t firstUsedEntry;

	static constexpr auto HASH_TABLE_SIZE = 89; // A prime number
	int8_t hashTable[HASH_TABLE_SIZE];

	uint32_t currGenerationTag;

public:
	CommandHandlersRegistry( GenericClientProtocolExecutor *executor_, const char *tag_ );

	// Looks like there is no covariance in pointers to methods,
	// so GenericClientProtocolExecutor methods are not compatible with AbstractClientProtocolExecutor ones
	// TODO: Investigate
	void Register( const char *name, CommandHandler handler );

	void NewGenerationTag() { currGenerationTag++; }
	unsigned CurrGenerationTag() { return currGenerationTag; }

	bool HandleCommand( CommandParser &parser );

	void Clear( unsigned tag );
};

class GenericClientProtocolExecutor : public AbstractClientProtocolExecutor, ChannelListener
{
	friend class CommandBuffer;
	friend class Client;

protected:
	enum ClientState {
		CA_DISCONNECTED,
		CA_SETUP,
		CA_CHALLENGING,
		CA_CONNECTING,
		CA_LOADING,
		CA_CONFIGURING,
		CA_ENTERING,
		CA_ACTIVE
	};

	Channel channel;
	CommandBuffer commandBuffer;

	CommandHandlersRegistry serverCommandHandlers;
	CommandHandlersRegistry clientCommandHandlers;

	ClientWorldState *worldState;
	MessageParser *messageParser;

	ClientState clientState;
	const int protocolVersion;

	uint64_t resendAt;
	uint64_t lastSentAt;

	NetworkAddress currServerAddress;

	void SetState( ClientState clientState_, uint64_t resendAt_ = 0 ) {
		this->clientState = clientState_;
		this->resendAt = resendAt_;
	}

	uint64_t Millis() const { return system->Millis(); }

	GenericClientProtocolExecutor( Client *client_,
								   ClientWorldState *worldState_,
								   MessageParser *messageParser_,
								   int protocolVersion_ );

	char name[MAX_STRING_CHARS];
	char password[MAX_STRING_CHARS];
	char challenge[MAX_STRING_CHARS];
	char session[MAX_STRING_CHARS];

	void SetName( const char *name_ ) {
		QStrncpyz( this->name, name_, MAX_STRING_CHARS );
	}

	void SetPassword( const char *password_ ) {
		QStrncpyz( this->password, password_, MAX_STRING_CHARS );
	}

	void Command_Connect( CommandParser &parser );
	void Command_Connect( const UnresolvedAddress &unresolvedAddress );
	void Command_Connect( const NetworkAddress &address );

	void Command_Disconnect( CommandParser &parser );
	void Command_Disconnect();

	void DoChallengeRequest();
	void DoConnectRequest();
	void DoDisconnectRequest();

	void Send() {
		channel.Send();
		lastSentAt = Millis();
	}

	/**
	 * Starts sending a "challenge" request to a server
	 */
	void ServerCommand_Challenge( CommandParser &parser );

	/**
	 * Starts sending a "client_connect" request to a server
	 */
	void ServerCommand_ClientConnect( CommandParser &parser );

	/**
	 * Starts sending a "client_connect" request to a server
	 */
	void ServerCommand_ClientConnect();

	/**
	 * Sends a configstrings request to a server
	 */
	void ServerCommand_Cs( CommandParser &parser );

	/**
	 * Executes an arbitrary command remotely supplied by a server
	 */
	void ServerCommand_Cmd( CommandParser &parser );

	/**
	 * Executes a "precache" command, actually enters the game.
	 */
	void ServerCommand_Precache( CommandParser &parser );

	/**
	 * Handles a "disconnect" command sent by a server
	 */
	void ServerCommand_Disconnect( CommandParser &parser );

	/**
	 * Handles a "reject" command sent by a server
	 */
	void ServerCommand_Reject( CommandParser &parser );

	/**
	 * Handles a "force reconnect" command sent by a server
	 */
	void ServerCommand_ForceReconnect( CommandParser &parser );

	/**
	 * Handles a "reconnect" command supplied by a server
	 */
	void ServerCommand_Reconnect( CommandParser &parser );

	/**
	 * Executes a "print to console" command sent by a server
	 */
	void ServerCommand_Pr( CommandParser &parser );

	/**
	 * Executes a "print centered UI message" command sent by a server
	 */
	void ServerCommand_Print( CommandParser &parser );

	/**
	 * Executes a "print a chat message" command sent by a server
	 */
	void ServerCommand_Ch( CommandParser &parser );

	/**
	 * Executes a "print a team chat message" command sent by a server
	 */
	void ServerCommand_Tch( CommandParser &parser );

	/**
	 * Executes a "print a TV chat message" command sent by a server
	 */
	void ServerCommand_Tvch( CommandParser &parser );

	/**
	 * Sets the "message of the day" sent by a server
	 */
	void ServerCommand_Motd( CommandParser &parser );

	typedef void (Client::*ClientChatHandler)( const char *, const char * );
	void HandleServerChatCommand( CommandParser &parser, ClientChatHandler handler );

#ifndef _MSC_VER
	void EnqueueCommand( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
#else
	void EnqueueCommand( _Printf_format_string_ const char *format, ... );
#endif

	void AddMove( Message &message, int64_t lastFrame, uint64_t serverTime );

public:
	~GenericClientProtocolExecutor() override;

	static GenericClientProtocolExecutor *New( Console *console_, Client *client_, System *system_, int protocolVersion );
	static void Delete( GenericClientProtocolExecutor *executor );

	void OnIngoingSequencedMessage( Message &message ) override;
	void OnIngoingNonSequencedMessage( Message &message ) override;

	void SendCommandAck( int64_t ackNum );
	void SendFrameAck( int64_t lastFrame, uint64_t serverTime );
	void TryAcknowledge( int64_t ackNum );

	void Enter();
	void Activate();

	void Reset() override;

	void Frame() override;

	void ExecuteCommandFromServer( const char *command );
	void ExecuteCommandFromClient( const char *command ) override;
};

#endif
