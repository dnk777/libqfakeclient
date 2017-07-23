#ifndef LIBQFAKECLIENT_CLIENT_H
#define LIBQFAKECLIENT_CLIENT_H

#include "channel.h"
#include "message_parser.h"
#include "network_address.h"
#include "protocol_executor.h"

class ClientListener
{
public:
	virtual ~ClientListener() {}

	virtual void SetShownPlayerName( const char *name ) = 0;
	virtual void SetMessageOfTheDay( const char *motd ) = 0;
	virtual void PrintCenteredMessage( const char *message ) = 0;
	virtual void PrintChatMessage( const char *from, const char *message ) = 0;
	virtual void PrintTeamChatMessage( const char *from, const char *message ) = 0;
	virtual void PrintTVChatMessage( const char *from, const char *message ) = 0;
};

class Client
{
	friend class System;
	friend class CommandBuffer;
	friend class CommandHandlersRegistry;
	friend class MessageParser;

	Console *console;
	System *system;
	ClientListener *listener;
	GenericClientProtocolExecutor *protocolExecutor;

	int oldProtocolVersion;
	int protocolVersion;

	char name[MAX_STRING_CHARS];
	char password[MAX_STRING_CHARS];

	Client( Console *console_, System *system_ );

	~Client() {
		Reset();
	}

	bool CheckExecutor();
	void AttachExecutor();
	void DetachExecutor();

	void PrintMissingListenerWarning( const char *function );

	void CheckThread( const char *function );

public:
	void ExecuteCommand( const char *command );
	void Reset();
	void Frame();

	// Resolve a name clash by adding a getter prefix
	Console *GetConsole() { return console; }
	System *GetSystem() { return system; }

	void SetListener( ClientListener *listener_ ) {
		this->listener = listener_;
	}

	void SetShownPlayerName( const char *name );
	void SetMessageOfTheDay( const char *motd );

	void PrintCenteredMessage( const char *message );
	void PrintChatMessage( const char *from, const char *message );
	void PrintTeamChatMessage( const char *from, const char *message );
	void PrintTVChatMessage( const char *from, const char *message );
};

#endif
