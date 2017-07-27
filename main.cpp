#include "system.h"
#include "client.h"
#include "command_parser.h"

#include <stdio.h>

class TaggedConsole : public Console
{
	const char *tag;
	void VPrintf( const char *format, va_list va ) override {
		fputs( tag, stdout );
		fputs( ": ", stdout );
		vfprintf( stdout, format, va );
	}

public:
	TaggedConsole( const char *tag_ ) : tag( tag_ ) {}
};

int main( int argc, const char **argv ) {
	auto *globalConsole = new( malloc( sizeof( TaggedConsole ) ) )TaggedConsole( "System" );
	auto *clientConsole = new( malloc( sizeof( TaggedConsole ) ) )TaggedConsole( "Client" );

	System::Init( globalConsole );
	System *system = System::Instance();
	Client *client = system->NewClient( clientConsole );

	client->ExecuteCommand( "connect 127.0.0.1" );

	for( int i = 0; i < 60; ++i ) {
		system->Sleep( 500 );
		system->Frame( 16 );
	}
	client->ExecuteCommand( "disconnect" );

	system->DeleteClient( client );
	System::Shutdown();
	return 0;
}
