#ifndef LIBQFAKECLIENT_COMMON_H
#define LIBQFAKECLIENT_COMMON_H

#include <stdint.h>
#include <stddef.h>

// Max fake client instances supported by this library
constexpr const unsigned MAX_FAKE_CLIENT_INSTANCES = 4;

// Max clients on a game server
constexpr const unsigned MAX_SERVER_CLIENTS = 256;

constexpr const unsigned PROTOCOL21 = 22;

constexpr const unsigned DEFAULT_PORT = 44400;

constexpr const unsigned MAX_MSGLEN = 65536;

constexpr const unsigned MAX_STRING_CHARS = 2048;
constexpr const unsigned MAX_MSG_STRING_CHARS = 2048;

constexpr const unsigned MAX_CONFIGSTRING_CHARS = 512;

constexpr const unsigned TIMEOUT = 1800;

constexpr const unsigned INACTIVE_TIME = 30000;

constexpr const unsigned FRAGMENT_BIT = 1u << 31;
constexpr const unsigned FRAGMENT_LAST = 1u << 14;

// TODO: Should be protocol-specific
enum {
	CLC_BAD,
	CLC_NOP,
	CLC_MOVE,
	CLC_SVACK,
	CLC_CLIENT_COMMAND,
	CLC_EXTENSION
};

// TODO: Should be protocol-specific
enum {
	DROP_TYPE_GENERAL,
	DROP_TYPE_PASSWORD,
	DROP_TYPE_RECONNECT,
};

constexpr unsigned DROP_FLAG_AUTORECONNECT = 1;

#include <stdio.h>

inline void QStrncpyz( char *dest, const char *src, size_t bufferSize ) {
	const char *const start = src;

	while( *src && src - start < bufferSize ) {
		*dest++ = *src++;
	}

	if( !*src ) {
		if( ( src - start ) < bufferSize ) {
			*dest = '\0';
		}
	}

	if( bufferSize > 0 ) {
		*dest = '\0';
	}
}

#endif
