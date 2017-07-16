#ifndef LIBQFAKECLIENT_SOCKET_H
#define LIBQFAKECLIENT_SOCKET_H

#include "network_address.h"

class Socket
{
	friend class System;
	void *underlying;
	bool isIpV4Socket;

	int UnderlyingFd() { return (int)(intmax_t)underlying; }

public:
	bool IsIpV4Socket() const { return isIpV4Socket; }

	bool SendDatagram( const NetworkAddress &address, const uint8_t *data, unsigned dataSize );
};

#endif
