#ifndef LIBQFAKECLIENT_CONSOLE_H
#define LIBQFAKECLIENT_CONSOLE_H

#include <stdarg.h>

class Console
{
public:
	virtual ~Console() {}

#ifndef _MSC_VER
	virtual void Printf( const char *format, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
#else
	virtual void Printf( _Printf_format_string_ const char *format, ... );
#endif

	virtual void VPrintf( const char *format, va_list va ) = 0;
};

#endif
