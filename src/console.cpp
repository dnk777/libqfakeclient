#include "console.h"

#include <stdarg.h>
#include <stdio.h>

void Console::Printf( const char *format, ... ) {
	va_list va;

	va_start( va, format );
	VPrintf( format, va );
	va_end( va );
}
