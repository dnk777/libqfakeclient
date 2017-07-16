#ifndef LIBQFAKECLIENT_COMMAND_PARSER_H
#define LIBQFAKECLIENT_COMMAND_PARSER_H

#include "common.h"

#include <stdint.h>

class CommandParser
{
	char tokenBuffer[MAX_STRING_CHARS + 1];
	const char *cmdCharsPtr;
	const char *argCharsPtr;

	const char *GetBasicArg( const char *tokenStart, unsigned *tokenLength = nullptr, uint32_t *tokenHash = nullptr );
	const char *GetQuotedArg( const char *tokenStart, unsigned *tokenLength = nullptr, uint32_t *tokenHash = nullptr );

public:
	CommandParser( const char *input_ ) : cmdCharsPtr( input_ ) {}

	const char *GetCommand( unsigned *tokenLength = nullptr, uint32_t *tokenHash = nullptr );
	const char *GetArg( unsigned *tokenLength = nullptr, uint32_t *tokenHash = nullptr );
};

uint32_t GetStringHashAndLength( const char *s, unsigned *length = nullptr );
uint32_t GetStringHashForGivenLength( const char *s, unsigned length );

#endif
