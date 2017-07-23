#include "client.h"

Client::Client( Console *console_, System *system_ )
	: console( console_ ),
	system( system_ ),
	listener( nullptr ),
	protocolExecutor( nullptr ),
	oldProtocolVersion( PROTOCOL21 ),
	protocolVersion( PROTOCOL21 ) {
	name[0] = 0;
	password[0] = 0;
}

void Client::Reset() {
	CheckThread( "Client::Reset()" );

	if( protocolExecutor ) {
		DetachExecutor();
		GenericClientProtocolExecutor::Delete( protocolExecutor );
		protocolExecutor = nullptr;
	}

	oldProtocolVersion = protocolVersion;
	protocolVersion = PROTOCOL21;
}

void Client::CheckThread( const char *function ) {
	this->system->CheckThread( function );
}

void Client::Frame() {
	CheckThread( "Client::Frame()" );

	if( protocolExecutor ) {
		protocolExecutor->Frame();
	}
}

void Client::DetachExecutor() {
}

bool Client::CheckExecutor() {
	if( protocolExecutor ) {
		return true;
	}

	protocolExecutor = GenericClientProtocolExecutor::New( console, this, system, protocolVersion );

	if( !protocolExecutor ) {
		return false;
	}

	AttachExecutor();
	return true;
}

void Client::AttachExecutor() {
	protocolExecutor->SetName( this->name );
	protocolExecutor->SetPassword( this->password );
}

void Client::ExecuteCommand( const char *command ) {
	CheckThread( "Client::ExecuteCommand()" );

	if( CheckExecutor() ) {
		protocolExecutor->ExecuteCommandFromClient( command );
	}
}

void Client::PrintMissingListenerWarning( const char *function ) {
	console->Printf( "Warning: %s: client listener is not set\n", function );
}

void Client::SetShownPlayerName( const char *name ) {
	if( listener ) {
		listener->SetShownPlayerName( name );
	} else {
		PrintMissingListenerWarning( "Client::SetShownPlayerName()" );
		console->Printf( "Shown player name: `%s`\n", name );
	}
}

void Client::SetMessageOfTheDay( const char *motd ) {
	if( listener ) {
		listener->SetMessageOfTheDay( motd );
	} else {
		PrintMissingListenerWarning( "Client::SetMessageOfTheDay()" );
		console->Printf( "Message of the day: `%s`\n", motd );
	}
}

void Client::PrintCenteredMessage( const char *message ) {
	if( listener ) {
		listener->PrintCenteredMessage( message );
	} else {
		PrintMissingListenerWarning( "Client::PrintCenteredMessage()" );
		console->Printf( "Centered message: `%s`\n", message );
	}
}

void Client::PrintChatMessage( const char *from, const char *message ) {
	if( listener ) {
		listener->PrintChatMessage( from, message );
	} else {
		PrintMissingListenerWarning( "Client::PrintChatMessage()" );
		console->Printf( "Chat from `%s`: `%s`\n", from, message );
	}
}

void Client::PrintTeamChatMessage( const char *from, const char *message ) {
	if( listener ) {
		listener->PrintTeamChatMessage( from, message );
	} else {
		PrintMissingListenerWarning( "Client::PrintTeamChatMessage()" );
		console->Printf( "Team chat from `%s`: `%s`\n", from, message );
	}
}

void Client::PrintTVChatMessage( const char *from, const char *message ) {
	if( listener ) {
		listener->PrintTVChatMessage( from, message );
	} else {
		PrintMissingListenerWarning( "Client::PrintTVChatMessage()" );
		console->Printf( "TV chat from `%s`: `%s`\n", from, message );
	}
}
