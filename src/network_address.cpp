#include "common.h"
#include "network_address.h"
#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <network_address.h>
#else
#error There is no Windows-compatible version yet
#endif

UnresolvedAddress::UnresolvedAddress( const char *string ) {
	const char *semicolon = nullptr;
	const char *s = string;
	const char *openingBracket = nullptr;
	const char *closingBracket = nullptr;
	char buffer[INET6_ADDRSTRLEN + 1];
	char *endptr;

	hasParsingErrors = false;
	isResolved = false;

	while( *s ) {
		if( *s == ':' ) {
			semicolon = s;
		} else if( *s == '[' ) {
			if( !openingBracket ) {
				openingBracket = s;
			} else {
				hasParsingErrors = true;
				return;
			}
		} else if( *s == ']' ) {
			if( !closingBracket ) {
				closingBracket = s;
			} else {
				hasParsingErrors = true;
				return;
			}
		}
		s++;
	}
	auto length = s - string;

	if( ( openingBracket == nullptr ) ^ ( closingBracket == nullptr ) ) {
		hasParsingErrors = true;
		return;
	}

	if( !closingBracket ) {
		// Try parse as an IP v4 address without a port
		if( address.TryParseAs( AF_INET, DEFAULT_PORT, string ) ) {
			isResolved = true;
			return;
		}

		if( !semicolon ) {
			hasParsingErrors = true;
			return;
		}

		// Try parse as an IP v4 address with a port
		long port = strtol( semicolon + 1, &endptr, 10 );

		if( port > 0 && port < std::numeric_limits<uint16_t>::max() && !endptr[0] ) {
			memcpy( buffer, string, semicolon - string );
			buffer[semicolon - string] = 0;

			if( address.TryParseAs( AF_INET, (uint16_t)port, buffer ) ) {
				isResolved = true;
				return;
			}
		}

		// Try parse as an IP v6 address without a port
		if( address.TryParseAs( AF_INET6, DEFAULT_PORT, string ) ) {
			isResolved = true;
			return;
		}

		// Treat the address as unresolved one
		return;
	}

	if( openingBracket >= closingBracket ) {
		hasParsingErrors = true;
		return;
	}

	if( semicolon > closingBracket ) {
		// Try parse as an IP v6 address with a port
		memcpy( buffer, openingBracket + 1, closingBracket - openingBracket - 1 );
		buffer[closingBracket - openingBracket - 1] = 0;
		long port = strtol( semicolon + 1, &endptr, 10 );

		if( port > 0 && port < std::numeric_limits<uint16_t>::max() && !endptr[0] ) {
			if( address.TryParseAs( AF_INET6, (uint16_t)port, buffer ) ) {
				isResolved = true;
				return;
			}
		}
	}

	// Try parse as an IP v6 address enclosed in brackets but without a port
	// TODO: Not sure if this allowed by the RFC, but accept it
	if( closingBracket == &string[length - 1] ) {
		memcpy( buffer, openingBracket + 1, closingBracket - openingBracket - 1 );
		buffer[closingBracket - openingBracket - 1] = 0;

		if( address.TryParseAs( AF_INET6, DEFAULT_PORT, buffer ) == 1 ) {
			isResolved = true;
			return;
		}
	}

	// Treat the address as unresolved one
}

NetworkAddress UnresolvedAddress::ToResolvedAddress() const {
	if( !IsResolved() ) {
		Console *systemConsole = System::Instance()->SystemConsole();
		systemConsole->Printf( "UnresolvedAddress::ToResolvedAddress(): an address is not resolved\n" );
	}
	return address;
}
