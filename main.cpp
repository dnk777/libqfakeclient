#include "system.h"
#include "client.h"
#include "server_list.h"

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

class DummyServerListListener : public ServerListListener
{
public:
	void OnServerAdded( const PolledGameServer &server ) override {
		printf( "A server %p (%s) has been added\n", &server, server.ServerName().Get() );
	}
	void OnServerRemoved( const PolledGameServer &server ) override {
		printf( "A server %p has been removed\n", &server );
	}
	void OnServerUpdated( const PolledGameServer &server ) override {
		printf( "A server %p has been updated\n", &server );
	}
};

int main( int argc, const char **argv ) {
	auto *globalConsole = new( malloc( sizeof( TaggedConsole ) ) )TaggedConsole( "System" );

	System::Init( globalConsole );
	System *system = System::Instance();

	UnresolvedAddress master1Address( "188.226.221.185:27950" );
	assert( master1Address.IsValidAsString() && master1Address.IsResolved() );
	UnresolvedAddress master2Address( "92.62.40.72:27950" );
	assert( master2Address.IsValidAsString() && master2Address.IsResolved() );

	assert( system->AddMasterServer( master1Address.ToResolvedAddress() ) );
	assert( system->AddMasterServer( master2Address.ToResolvedAddress() ) );

	system->SetServerListUpdateOptions( false, true );

	system->Frame( 16 );
	system->Sleep( 16 );

	auto *listener = new( malloc( sizeof( DummyServerListListener ) ) )DummyServerListListener;

	assert( system->StartUpdatingServerList( listener ) );

	for( int i = 0; i < 3000; ++i ) {
		printf( "Frame #%d\n", i + 1 );
		system->Sleep( 1000 - 16 );
		system->Frame( 16 );

		if( i >= 15 ) {
			system->SetServerListUpdateOptions( true, false );
		}
	}

	system->StopUpdatingServerList();

	System::Shutdown();
	return 0;
}
