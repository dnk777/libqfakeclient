#include "command_parser.h"

#include <assert.h>
#include <string.h>
#include <algorithm>

static inline void AddCharToHash( uint32_t *hash, char c ) {
	*hash = *hash * 31 + ( ( c << 24 ) ^ ~0 ) + c;
}

template <typename T1, typename T2>
static inline size_t MinSize( T1 a, T2 b ) {
	return std::min( (size_t)a, (size_t)b );
};

const char *CommandParser::GetCommand( unsigned *tokenLength, uint32_t *tokenHash ) {
	if( !cmdCharsPtr ) {
		return nullptr;
	}

	argCharsPtr = nullptr;

	// Make sure that length pointer is not null in the contained code
	unsigned length;

	if( !tokenLength ) {
		tokenLength = &length;
	}

	// Strip whitespace
	char ch;

	while( ( ch = *cmdCharsPtr ) ) {
		if( ch > ' ' ) {
			break;
		}
		cmdCharsPtr++;
	}

	switch( ch ) {
		case '\0':
			cmdCharsPtr = nullptr;

			if( tokenLength ) {
				*tokenLength = 0;
			}

			if( tokenHash ) {
				*tokenHash = 0;
			}
			return nullptr;
		case '\n':
		case ';':

			if( tokenLength ) {
				*tokenLength = 0;
			}

			if( tokenHash ) {
				*tokenHash = 0;
			}
			// Skip the char
			cmdCharsPtr++;
			return "";
		default:
			break;
	}

	const char *const start = cmdCharsPtr;

	if( !tokenHash ) {
		while( ( ch = *cmdCharsPtr ) ) {
			if( ch <= ' ' || ch == '\n' || ch == ';' || ch == '"' ) {
				break;
			}
			cmdCharsPtr++;
		}
	} else {
		*tokenHash = 0;

		while( ( ch = *cmdCharsPtr ) ) {
			if( ch <= ' ' || ch == '\n' || ch == ';' || ch == '"' ) {
				break;
			}
			cmdCharsPtr++;

			if( cmdCharsPtr - start <= sizeof( tokenBuffer ) ) {
				AddCharToHash( tokenHash, ch );
			}
		}
	}

	switch( ch ) {
		case '\0':
			*tokenLength = (unsigned)MinSize( cmdCharsPtr - start, sizeof( tokenBuffer ) );
			cmdCharsPtr = nullptr;
			argCharsPtr = nullptr;
			return start;
		case '\n':
		case ';':
			*tokenLength = (unsigned)MinSize( cmdCharsPtr - start, sizeof( tokenBuffer ) );
			QStrncpyz( tokenBuffer, start, *tokenLength );
			return tokenBuffer;
		default:
			break;
	}

	*tokenLength = (unsigned)MinSize( cmdCharsPtr - start, sizeof( tokenBuffer ) );
	QStrncpyz( tokenBuffer, start, *tokenLength );
	argCharsPtr = cmdCharsPtr;
	return tokenBuffer;
}

const char *CommandParser::GetArg( unsigned int *tokenLength, uint32_t *tokenHash ) {
	if( !cmdCharsPtr || !argCharsPtr ) {
		return nullptr;
	}

	unsigned length;

	if( !tokenLength ) {
		tokenLength = &length;
	}

	if( tokenHash ) {
		*tokenHash = 0;
	}

	// Skip whitespace
	char ch;

	while( ( ch = *argCharsPtr ) ) {
		if( ch > ' ' || ch == '\n' || ch == ';' ) {
			break;
		}
		argCharsPtr++;
		cmdCharsPtr++;
	}

	switch( ch ) {
		case '\0':
			cmdCharsPtr = nullptr;
			argCharsPtr = nullptr;
			return nullptr;
		case '\n':
		case ';':
			argCharsPtr = nullptr;
			// Skip the separator
			cmdCharsPtr++;
			return nullptr;
		case '"':
			// Skip first "
			argCharsPtr++;
			cmdCharsPtr++;
			return GetQuotedArg( argCharsPtr, tokenLength, tokenHash );
		default:
			return GetBasicArg( argCharsPtr, tokenLength, tokenHash );
	}
}

const char *CommandParser::GetBasicArg( const char *argStart, unsigned *argLength, uint32_t *argHash ) {
	assert( argCharsPtr );
	assert( cmdCharsPtr );

	while( char ch = *argCharsPtr ) {
		argCharsPtr++;
		cmdCharsPtr++;

		if( ch <= ' ' ) {
			*argLength = (unsigned)MinSize( argCharsPtr - 1u - argStart, sizeof( tokenBuffer ) );
			QStrncpyz( tokenBuffer, argStart, *argLength );
			return tokenBuffer;
		}

		if( ch == '\n' || ch == ';' ) {
			*argLength = (unsigned)MinSize( argCharsPtr - 1u - argStart, sizeof( tokenBuffer ) );
			QStrncpyz( tokenBuffer, argStart, *argLength );
			argCharsPtr = nullptr;
			return tokenBuffer;
		}

		if( ch == '"' ) {
			*argLength = (unsigned)MinSize( argCharsPtr - 1u - argStart, sizeof( tokenBuffer ) );
			QStrncpyz( tokenBuffer, argStart, *argLength );
			// Start with '"' at next GetArg() call
			argCharsPtr--;
			cmdCharsPtr--;
			return tokenBuffer;
		}

		if( argHash ) {
			AddCharToHash( argHash, ch );
		}
	}

	// Roll back for convenience
	argCharsPtr--;
	// We have reached the string end
	cmdCharsPtr = nullptr;

	// The token is terminated by a trailing zero.
	// Do not make a copy in this case, return a pointer to the token part of the cmdCharsPtr.
	// The only exception is way too large token (a caller expects tokens of a limited size).
	if( argCharsPtr - argStart < sizeof( tokenBuffer ) ) {
		if( argLength ) {
			*argLength = (unsigned)( argCharsPtr - argStart + 1 );
		}
		// This is the last arg for the command
		argCharsPtr = nullptr;
		return argStart;
	}

	size_t length = MinSize( argCharsPtr - argStart + 1, sizeof( tokenBuffer ) );
	QStrncpyz( tokenBuffer, argStart, length );

	if( argLength ) {
		*argLength = (unsigned)length;
	}
	// This is the last arg for the command
	argCharsPtr = nullptr;
	return tokenBuffer;
}

const char *CommandParser::GetQuotedArg( const char *argStart, unsigned *argLength, uint32_t *argHash ) {
	assert( argCharsPtr );
	assert( cmdCharsPtr );

	while( char ch = *argCharsPtr ) {
		argCharsPtr++;
		cmdCharsPtr++;

		if( ch == '"' ) {
			*argLength = (unsigned)MinSize( sizeof( tokenBuffer ) - 1u, argCharsPtr - argStart - 1 );
			QStrncpyz( tokenBuffer, argStart, *argLength );
			return tokenBuffer;
		}

		if( argHash ) {
			AddCharToHash( argHash, ch );
		}
	}

	*argLength = (unsigned)MinSize( sizeof( tokenBuffer ) - 1u, argCharsPtr - argStart - 1 );
	QStrncpyz( tokenBuffer, argStart + 1, *argLength );
	// We have reached the string end, there are no further commands
	cmdCharsPtr = nullptr;
	argCharsPtr = nullptr;
	return tokenBuffer;
}

uint32_t GetStringHashAndLength( const char *s, unsigned *length ) {
	const char *startPtr = s;
	uint32_t hash = 0;

	while( *s ) {
		AddCharToHash( &hash, *s );
		s++;
	}

	if( length ) {
		*length = (unsigned)( s - startPtr );
	}
	return hash;
}

uint32_t GetStringHashForGivenLength( const char *s, unsigned length ) {
	uint32_t hash = 0;

	for( unsigned i = 0; i < length; ++i ) {
		AddCharToHash( &hash, s[i] );
	}
	return hash;
}
