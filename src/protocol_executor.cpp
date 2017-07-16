#include "client.h"
#include "command_parser.h"
#include "message_parser.h"
#include "protocol_executor.h"

#include <new>
#include <limits>
#include <stdlib.h>

AbstractClientProtocolExecutor::AbstractClientProtocolExecutor( Client *client_ )
	: client( client_ ),
	console( client_->GetConsole() ),
	system( client_->GetSystem() ),
	configStringsData( nullptr ),
	maxConfigStrings( 0 ),
	configStringsStride( 0 ) {
}

GenericClientProtocolExecutor::GenericClientProtocolExecutor( Client *client_,
															  ClientWorldState *worldState_,
															  MessageParser *messageParser_,
															  int protocolVersion_ )
	: AbstractClientProtocolExecutor( client_ ),
	channel( console, system, this ),
	commandBuffer( console, system, this ),
	serverCommandHandlers( this, "trying to execute a server command" ),
	clientCommandHandlers( this, "trying to execute a command" ),
	protocolVersion( protocolVersion_ ),
	worldState( worldState_ ),
	messageParser( messageParser_ ) {

	// Should be set by the client later
	name[0] = 0;
	password[0] = 0;

	typedef GenericClientProtocolExecutor GPTE;

	serverCommandHandlers.Register( "challenge", &GPTE::ServerCommand_Challenge );
	serverCommandHandlers.Register( "client_connect", &GPTE::ServerCommand_ClientConnect );
	serverCommandHandlers.Register( "cs", &GPTE::ServerCommand_Cs );
	serverCommandHandlers.Register( "cmd", &GPTE::ServerCommand_Cmd );
	serverCommandHandlers.Register( "precache", &GPTE::ServerCommand_Precache );
	serverCommandHandlers.Register( "disconnect", &GPTE::ServerCommand_Disconnect );
	serverCommandHandlers.Register( "reject", &GPTE::ServerCommand_Reject );
	serverCommandHandlers.Register( "forcereconnect", &GPTE::ServerCommand_ForceReconnect );
	serverCommandHandlers.Register( "reconnect", &GPTE::ServerCommand_Reconnect );

	serverCommandHandlers.Register( "pr", &GPTE::ServerCommand_Pr );
	serverCommandHandlers.Register( "print", &GPTE::ServerCommand_Print );
	serverCommandHandlers.Register( "ch", &GPTE::ServerCommand_Ch );
	serverCommandHandlers.Register( "tch", &GPTE::ServerCommand_Tch );
	serverCommandHandlers.Register( "tvch", &GPTE::ServerCommand_Tvch );
	serverCommandHandlers.Register( "motd", &GPTE::ServerCommand_Motd );

	serverCommandHandlers.Register( "mm", nullptr );
	serverCommandHandlers.Register( "mapmsg", nullptr );
	serverCommandHandlers.Register( "plstats", nullptr );
	serverCommandHandlers.Register( "scb", nullptr );
	serverCommandHandlers.Register( "obry", nullptr );
	serverCommandHandlers.Register( "ti", nullptr );
	serverCommandHandlers.Register( "cvarinfo", nullptr );
	serverCommandHandlers.Register( "demoget", nullptr );
	serverCommandHandlers.Register( "cha", nullptr );
	serverCommandHandlers.Register( "chr", nullptr );
	serverCommandHandlers.Register( "mecu", nullptr );
	serverCommandHandlers.Register( "meop", nullptr );
	serverCommandHandlers.Register( "memo", nullptr );
	serverCommandHandlers.Register( "changing", nullptr );
	serverCommandHandlers.Register( "cp", nullptr );
	serverCommandHandlers.Register( "cpf", nullptr );
	serverCommandHandlers.Register( "aw", nullptr );
	serverCommandHandlers.Register( "qm", nullptr );

	serverCommandHandlers.Register( "dstart", nullptr );
	serverCommandHandlers.Register( "dstop", nullptr );
	serverCommandHandlers.Register( "dcancel", nullptr );
	serverCommandHandlers.Register( "cpc", nullptr );
	serverCommandHandlers.Register( "cpa", nullptr );

	serverCommandHandlers.NewGenerationTag();

	clientCommandHandlers.Register( "connect", &GPTE::Command_Connect );
	clientCommandHandlers.Register( "disconnect", &GPTE::Command_Disconnect );

	clientCommandHandlers.NewGenerationTag();

	Reset();
}

void GenericClientProtocolExecutor::Command_Connect( CommandParser &parser ) {
	const char *arg = parser.GetArg();

	if( !arg ) {
		console->Printf( "Cannot execute `connect` command: the address is not specified\n" );
		return;
	}

	UnresolvedAddress unresolvedAddress( arg );

	if( !unresolvedAddress.IsValidAsString() ) {
		console->Printf( "Cannot execute `connect` command: illegal address `%s`\n", arg );
		return;
	}

	Command_Connect( unresolvedAddress );
}

void GenericClientProtocolExecutor::Command_Connect( const UnresolvedAddress &unresolvedAddress ) {
	if( !unresolvedAddress.IsResolved() ) {
		console->Printf( "Cannot execute `connect` command: DNS address resolution is not supported yet\n" );
		return;
	}

	NetworkAddress address( unresolvedAddress.ToResolvedAddress() );
	Command_Connect( address );
}

void GenericClientProtocolExecutor::Command_Connect( const NetworkAddress &address ) {
	if( !channel.PrepareForAddress( address ) ) {
		return;
	}

	currServerAddress = address;
	channel.StartListening();
	DoChallengeRequest();
}

void GenericClientProtocolExecutor::Command_Disconnect( CommandParser &parser ) {
	Command_Disconnect();
}

void GenericClientProtocolExecutor::Command_Disconnect() {
	if( clientState == CA_DISCONNECTED ) {
		return;
	}

	DoDisconnectRequest();
	channel.StopListening();
}

void GenericClientProtocolExecutor::DoChallengeRequest() {
	console->Printf( "Requesting challenge...\n" );
	Message &message = channel.PrepareNonSequencedOutgoingMessage();
	message.WriteString( "getchallenge" );
	Send();
	SetState( CA_CHALLENGING, Millis() + TIMEOUT );
}

void GenericClientProtocolExecutor::DoConnectRequest() {
	console->Printf( "Sending connection request...\n" );
	Message &message = channel.PrepareNonSequencedOutgoingMessage();
	const int port = channel.NatPunchthroughPort();
	constexpr const char *format = "connect %d %d %s \"\\name\\%s\\password\\%s\" 0";
	message.Printf( format, protocolVersion, port, challenge, name, password );
	Send();
	SetState( CA_CONNECTING, Millis() + TIMEOUT );
}

void GenericClientProtocolExecutor::DoDisconnectRequest() {
	console->Printf( "Disconnecting...\n" );

	for( unsigned i = 0; i < 3; ++i ) {
		Message &message = channel.PrepareNonSequencedOutgoingMessage();
		message.WriteString( "disconnect" );
		Send();
	}
	SetState( CA_DISCONNECTED );
}

GenericClientProtocolExecutor *GenericClientProtocolExecutor::New( Console *console, Client *client, System *system, int protocolVersion ) {
	if( protocolVersion != PROTOCOL21 ) {
		return nullptr;
	}

	void *mem = malloc( sizeof( GenericClientProtocolExecutor ) );

	if( !mem ) {
		return nullptr;
	}

	ClientWorldState *worldState = ClientWorldState::New( protocolVersion );

	if( !worldState ) {
		free( mem );
		return nullptr;
	}

	MessageParser *messageParser = MessageParser::New( protocolVersion, client, worldState, console );

	if( !messageParser ) {
		free( mem );
		ClientWorldState::Delete( worldState );
		return nullptr;
	}

	return new(mem)GenericClientProtocolExecutor( client, worldState, messageParser, PROTOCOL21 );
}

GenericClientProtocolExecutor::~GenericClientProtocolExecutor() {
	ClientWorldState::Delete( worldState );
	MessageParser::Delete( messageParser );
}

void GenericClientProtocolExecutor::SendCommandAck( int64_t ackNum ) {
	if( protocolVersion <= PROTOCOL21 && ackNum > std::numeric_limits<int>::max() ) {
		console->Printf( "GenericClientProtocolExecutor::SendCommandAck(): integer overflow\n" );
		return;
	}

	Message &message = channel.PrepareSequencedOutgoingMessage();
	message.WriteByte( CLC_SVACK );
	message.WriteLong( (int)ackNum );
	Send();
}

void GenericClientProtocolExecutor::SendFrameAck( int64_t lastFrame, uint64_t serverTime ) {
	Message &message = channel.PrepareSequencedOutgoingMessage();

	if( protocolVersion <= PROTOCOL21 ) {
		if( lastFrame > std::numeric_limits<int>::max() ) {
			console->Printf( "GenericClientProtocolExecutor::SendFrameAck(): integer overflow on `lastFrame` arg\n" );
			return;
		}

		if( serverTime > std::numeric_limits<int>::max() ) {
			console->Printf( "GenericClientProtocolExecutor::SendFrameAck(): integer overflow on `serverTime` arg\n" );
			return;
		}
	}

	messageParser->lastFrame = lastFrame;
	messageParser->serverTime = serverTime;
	AddMove( message, lastFrame, serverTime );
	Send();
}

void GenericClientProtocolExecutor::TryAcknowledge( int64_t ackNum ) {
	commandBuffer.TryAcknowledge( ackNum );
}

void GenericClientProtocolExecutor::AddMove( Message &message, int64_t lastFrame, uint64_t serverTime ) {
	// TODO: Check ucmd handling in 2.1+ versions (not to mention lastFrame/serverTime bits count)
	assert( protocolVersion <= PROTOCOL21 );
	message.WriteByte( CLC_MOVE );
	message.WriteLong( (int)lastFrame );
	message.WriteLong( 2 );
	message.WriteByte( 1 );
	message.WriteByte( 0 );
	message.WriteLong( (int)serverTime );
}

void GenericClientProtocolExecutor::Activate() {
	if( clientState != CA_ENTERING ) {
		return;
	}

	SetState( CA_ACTIVE );
}

void GenericClientProtocolExecutor::Delete( GenericClientProtocolExecutor *executor ) {
	if( !executor ) {
		return;
	}

	executor->~GenericClientProtocolExecutor();
	free( executor );
}

void GenericClientProtocolExecutor::OnIngoingSequencedMessage( Message &message ) {
	messageParser->Parse( message );
}

void GenericClientProtocolExecutor::OnIngoingNonSequencedMessage( Message &message ) {
	CommandParser parser( message.ReadString() );

	serverCommandHandlers.HandleCommand( parser );
}

void GenericClientProtocolExecutor::Reset() {
	clientState = CA_DISCONNECTED;

	worldState->Clear();

	configStringsData = worldState->ConfigStringsData();
	configStringsStride = worldState->ConfigStringsStride();
	maxConfigStrings = worldState->MaxConfigStrings();

	for( unsigned i = 0; i < maxConfigStrings; ++i )
		ConfigString( i )[0] = 0;

	serverCommandHandlers.Clear( serverCommandHandlers.CurrGenerationTag() );
	clientCommandHandlers.Clear( clientCommandHandlers.CurrGenerationTag() );

	channel.Reset();
	commandBuffer.Reset();
}

void GenericClientProtocolExecutor::Frame() {
	if( clientState <= CA_DISCONNECTED ) {
		return;
	}

	commandBuffer.ResendBufferedMessages();

	switch( clientState ) {
		case CA_CHALLENGING:

			if( Millis() >= resendAt ) {
				DoChallengeRequest();
			}
			break;
		case CA_CONNECTING:

			if( Millis() >= resendAt ) {
				DoConnectRequest();
			}
			break;
		case CA_LOADING:

			if( !worldState->PlayerNum() ) {
				return;
			}
			console->Printf( "Requesting configstrings...\n" );
			EnqueueCommand( "configstrings %d 0", worldState->SpawnCount() );
			SetState( CA_CONFIGURING );
			break;
		case CA_ACTIVE:

			if( Millis() >= lastSentAt + INACTIVE_TIME ) {
				Message &message = channel.PrepareSequencedOutgoingMessage();
				AddMove( message, messageParser->lastFrame, messageParser->serverTime );
				Send();
			}
			break;
		default:
			break;
	}
}

void GenericClientProtocolExecutor::ExecuteCommandFromServer( const char *command ) {
	CommandParser commandParser( command );

	serverCommandHandlers.HandleCommand( commandParser );
}

void GenericClientProtocolExecutor::ExecuteCommandFromClient( const char *command ) {
	CommandParser commandParser( command );

	clientCommandHandlers.HandleCommand( commandParser );
}

void CommandHandlersRegistry::Register( const char *name, CommandHandler handler ) {
	// Try find an existing command with the same name.
	for( unsigned i = 0; i < numHandlers; ++i ) {
		if( !strcmp( handlers[i].name, name ) ) {
			handlers[numHandlers].handler = handler;
			return;
		}
	}

	if( numHandlers == MAX_HANDLERS ) {
		executor->console->Printf( "CommandsHandlerRegistry::Register(): Too many command handlers\n" );
		return;
	}

	handlers[numHandlers].name = name;
	handlers[numHandlers].handler = handler;
	numHandlers++;
}

bool CommandHandlersRegistry::HandleCommand( CommandParser &parser ) {
	const char *commandName = parser.GetCommand();

	if( !commandName ) {
		executor->console->Printf( "%s: no command has been supplied\n", tag );
		return false;
	}

	for( unsigned i = 0; i < numHandlers; ++i ) {
		if( !strcmp( commandName, handlers[i].name ) ) {
			if( handlers[i].handler ) {
				( executor->*handlers[i].handler )( parser );
			}
			return true;
		}
	}

	// An empty command
	if( !commandName[0] ) {
		return true;
	}

	executor->console->Printf( "%s: unknown command %s\n", tag, commandName );
	return false;
}

void CommandHandlersRegistry::Clear( unsigned tag ) {
	unsigned i = 0;

	// TODO: Start from the last?
	while( i < numHandlers ) {
		if( handlers[i].tag == tag ) {
			handlers[i] = handlers[numHandlers - 1];
			numHandlers--;
		} else {
			i++;
		}
	}
}

void GenericClientProtocolExecutor::ServerCommand_Challenge( CommandParser &parser ) {
	const char *token = parser.GetCommand();

	if( !token ) {
		console->Printf( "Cannot execute server `challenge` command: missing an argument\n" );
		return;
	}

	QStrncpyz( challenge, token, sizeof( challenge ) );
	DoConnectRequest();
}

void GenericClientProtocolExecutor::ServerCommand_ClientConnect( CommandParser &parser ) {
	const char *token = parser.GetCommand();

	if( !token ) {
		console->Printf( "Cannot execute server `client_connect` command: missing an argument\n" );
		return;
	}

	QStrncpyz( session, token, sizeof( session ) );
	ServerCommand_ClientConnect();
}

void GenericClientProtocolExecutor::ServerCommand_ClientConnect() {
	console->Printf( "Sending serverdata request...\n" );
	EnqueueCommand( "new" );
	SetState( CA_LOADING );
}

void GenericClientProtocolExecutor::ServerCommand_Cs( CommandParser &parser ) {
	char *endptr;
	unsigned tokenLength;

	for(;; ) {
		const char *numToken = parser.GetArg();

		if( !numToken ) {
			break;
		}
		long num = strtol( numToken, &endptr, 10 );

		if( num < 0 || num >= worldState->MaxConfigStrings() || *endptr ) {
			console->Printf( "Cannot execute server 'cs' command: illegal configstring number %s\n", numToken );
			// TODO: Force disconnect?
			break;
		}
		const char *valueToken = parser.GetArg( &tokenLength );

		if( !valueToken ) {
			console->Printf( "Cannot execute server 'cs' command: missing confingstring value for string #%d\n", (int)num );
			// TODO: Force disconnect?
			break;
		}
		memcpy( ConfigString( (unsigned)num ), valueToken, tokenLength + 1 );
	}

	if( clientState > CA_DISCONNECTED ) {
		// TODO: Update client name
	}
}

void GenericClientProtocolExecutor::ServerCommand_Cmd( CommandParser &parser ) {
	unsigned tokenLength = 0;
	const char *token = parser.GetArg( &tokenLength );

	if( !token ) {
		console->Printf( "Cannot execute server 'cmd' command: an argument is missing\n" );
		// TODO: Force disconnect?
		return;
	}

	char buffer[MAX_STRING_CHARS];
	memcpy( buffer, token, tokenLength );
	unsigned totalLength = tokenLength;

	while( ( token = parser.GetArg( &tokenLength ) ) ) {
		if( totalLength + tokenLength + 3 >= MAX_STRING_CHARS ) {
			// TODO: Add handlers? It should never happen tbh
			abort();
		}
		buffer[totalLength++] = ' ';
		buffer[totalLength++] = '"';
		memcpy( buffer + totalLength, token, tokenLength );
		totalLength += tokenLength;
		buffer[totalLength++] = '"';
	}
	buffer[totalLength] = '\0';

	EnqueueCommand( "%s", buffer );
	resendAt = Millis() + TIMEOUT;
}

void GenericClientProtocolExecutor::ServerCommand_Precache( CommandParser &parser ) {
	if( clientState != CA_CONFIGURING ) {
		return;
	}

	// TODO inspect what does this hardcoded 0 configstring index mean? An absence of configstrings?
	if( !ConfigString( 0 )[0] ) {
		return;
	}

	Enter();
}

void GenericClientProtocolExecutor::Enter() {
	console->Printf( "Entering the game...\n" );
	EnqueueCommand( "begin %d", worldState->SpawnCount() );
	// multiview code has been removed
	SetState( CA_ENTERING );
}

void GenericClientProtocolExecutor::ServerCommand_Disconnect( CommandParser &parser ) {
	if( autoReconnect ) {
		ServerCommand_Reconnect( parser );
	} else {
		Command_Disconnect();
	}
}

void GenericClientProtocolExecutor::ServerCommand_Reject( CommandParser &parser ) {
	if( clientState > CA_CONNECTING ) {
		return;
	}

	const char *arg;
	char *endptr;

	if( !( arg = parser.GetCommand() ) ) {
		console->Printf( "Cannot execute server `reject` command: missing the drop type\n" );
		return;
	}

	long dropType = strtol( arg, &endptr, 10 );

	if( dropType < 0 || *endptr ) {
		console->Printf( "Cannot execute server `reject` command: illegal drop type token\n" );
		return;
	}

	if( !( arg = parser.GetCommand() ) ) {
		console->Printf( "Cannot execute server `reject` command: missing the drop flags\n" );
		return;
	}

	long dropFlags = strtol( arg, &endptr, 10 );

	if( dropFlags < 0 || *endptr ) {
		console->Printf( "Cannot execute server `reject` command: illegal drop flags token\n" );
		return;
	}

	if( !( arg = parser.GetCommand() ) ) {
		console->Printf( "Cannot execute server `reject command: missing the drop reason string\n`" );
		return;
	}

	console->Printf( "Rejected: %s\n", arg );
	Command_Disconnect();

	if( ( dropFlags & DROP_FLAG_AUTORECONNECT ) || autoReconnect ) {
		ServerCommand_ClientConnect();
	}
}

void GenericClientProtocolExecutor::ServerCommand_ForceReconnect( CommandParser &parser ) {
	NetworkAddress address( currServerAddress );

	Reset();
	Command_Connect( address );
}

void GenericClientProtocolExecutor::ServerCommand_Reconnect( CommandParser &parser ) {
	Command_Disconnect();
	ServerCommand_ClientConnect();
}

void GenericClientProtocolExecutor::ServerCommand_Pr( CommandParser &parser ) {
	if( const char *token = parser.GetArg() ) {
		console->Printf( "%s", token );
	}
}

void GenericClientProtocolExecutor::ServerCommand_Print( CommandParser &parser ) {
	if( const char *token = parser.GetArg() ) {
		client->PrintCenteredMessage( token );
	}
}

void GenericClientProtocolExecutor::HandleServerChatCommand( CommandParser &parser, ClientChatHandler handler ) {
	if( const char *token = parser.GetArg() ) {
		char from[MAX_STRING_CHARS];
		QStrncpyz( from, token, sizeof( from ) );

		if( const char *message = parser.GetArg() ) {
			( client->*handler )( from, message );
		}
	}
}

void GenericClientProtocolExecutor::ServerCommand_Ch( CommandParser &parser ) {
	HandleServerChatCommand( parser, &Client::PrintChatMessage );
}

void GenericClientProtocolExecutor::ServerCommand_Tch( CommandParser &parser ) {
	HandleServerChatCommand( parser, &Client::PrintTeamChatMessage );
}

void GenericClientProtocolExecutor::ServerCommand_Tvch( CommandParser &parser ) {
	HandleServerChatCommand( parser, &Client::PrintTVChatMessage );
}

void GenericClientProtocolExecutor::ServerCommand_Motd( CommandParser &parser ) {
	if( const char *token = parser.GetArg() ) {
		client->SetMessageOfTheDay( token );
	}
}

void GenericClientProtocolExecutor::EnqueueCommand( const char *format, ... ) {
	if( clientState < CA_SETUP ) {
		console->Printf( "Client::EnqueueCommand(): not connected\n" );
		return;
	}

	va_list va;
	va_start( va, format );

	if( worldState->IsConnectionReliable() ) {
		commandBuffer.EnqueueCommandForReliableConnectionV( format, va );
	} else {
		commandBuffer.EnqueueCommandForUnreliableConnectionV( format, va );
	}
	va_end( va );
}