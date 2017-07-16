#include "socket.h"
#include "system.h"
#include "client.h"
#include "console.h"

#include <assert.h>
#include <new>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#else
#error There is no Windows-compatible version yet
#endif

bool Socket::SendDatagram( const NetworkAddress &address, const uint8_t *data, unsigned dataSize ) {
	if( address.IsIpV4Address() ) {
		return sendto( UnderlyingFd(), data, dataSize, 0, address.AsGenericSockaddr(), sizeof( sockaddr_in ) ) >= 0;
	}

	return sendto( UnderlyingFd(), data, dataSize, 0, address.AsGenericSockaddr(), sizeof( sockaddr_in6 ) ) >= 0;
}

Socket *System::NewSocket( bool useIpV4 ) {
	int fd = socket( useIpV4 ? AF_INET : AF_INET6, SOCK_DGRAM, IPPROTO_UDP );

	if( fd < 0 ) {
		console->Printf( "System::NewSocket(): socket() syscall has failed\n" );
		return nullptr;
	}

	void *mem = malloc( sizeof( Socket ) );

	// If somebody has decided to turn memory overcommit off
	if( !mem ) {
		close( fd );
		console->Printf( "System::NewSocket(): cannot allocate a memory for a new socket\n" );
		return nullptr;
	}

	int on = 1;

	if( ioctl( fd, FIONBIO, &on ) < 0 ) {
		close( fd );
		console->Printf( "System::NewSocket(): cannot set non-blocking socket mode\n" );
		return nullptr;
	}

	Socket *result = new(mem)Socket();
	result->underlying = (void *)(intmax_t)fd;
	return result;
}

void System::DeleteSocket( Socket *socket ) {
	close( socket->UnderlyingFd() );
	socket->~Socket();
	free( socket );
}

void System::NetPollFrame( unsigned maxMillis ) {
	struct pollfd pollfds[MAX_SOCKETS];

	for( unsigned i = 0; i < numListenedSockets; ++i ) {
		struct pollfd *pfd = &pollfds[i];
		pfd->fd = listenedSockets[i].socket->UnderlyingFd();
		pfd->events = POLLIN;
		pfd->revents = 0;
	}

	int numfds = poll( pollfds, numListenedSockets, (int) maxMillis );

	if( numfds <= 0 ) {
		if( numfds < 0 ) {
			console->Printf( "System::NetPollFrame(): the poll() call has failed\n" );
		}
		return;
	}

	for( unsigned i = 0; i < numListenedSockets; ++i ) {
		struct pollfd *pfd = &pollfds[i];

		if( !( pfd->revents & POLLIN ) ) {
			continue;
		}
		OnSocketReadable( &listenedSockets[i] );
	}
}

void System::OnSocketReadable( ListenedSocket *listenedSocket ) {

	NetworkAddress address;
	int fd = listenedSocket->socket->UnderlyingFd();
	void *buffer = listenedSocket->buffer;
	size_t bufferSize = listenedSocket->bufferSize;

	for(;; ) {
		socklen_t addrLen = sizeof( sockaddr_in6 );
		ssize_t recvResult = recvfrom( fd, buffer, bufferSize, 0, address.AsGenericSockaddr(), &addrLen );

		if( recvResult <= 0 ) {
			if( recvResult < 0 ) {
				if( errno != EWOULDBLOCK ) {
					console->Printf( "System::NetPollFrame(): recvfrom() call has failed\n" );
				}
			}
			break;
		}

		if( address.IsIpV4Address() ) {
			listenedSocket->RunCallback( address, (unsigned)recvResult );
		} else if( address.IsIpV6Address() ) {
			listenedSocket->RunCallback( address, (unsigned)recvResult );
		} else {
			console->Printf( "System::NetPollFrame(): Unknown socket address length %d\n", (int)addrLen );
			break;
		}
	}
}
