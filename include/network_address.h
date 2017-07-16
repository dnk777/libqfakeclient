#ifndef LIBQFAKECLIENT_NETWORK_ADDRESS_H
#define LIBQFAKECLIENT_NETWORK_ADDRESS_H

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <string.h>

class alignas ( 8 )NetworkAddress
{
	friend class Channel;
	friend class Client;
	friend class System;
	friend class UnresolvedAddress;

	union {
		sockaddr_in in4;
		sockaddr_in6 in6;
	} u;

	bool TryParseAs( sa_family_t family, in_port_t port, const char *string ) {
		if( inet_pton( family, string, &u.in4.sin_addr ) == 1 ) {
			if( family == AF_INET ) {
				memset( u.in4.sin_zero, 0, sizeof( u.in4.sin_zero ) );
			} else {
				u.in6.sin6_flowinfo = 0;
				u.in6.sin6_scope_id = 0;
			}
			u.in4.sin_family = family;
			u.in4.sin_port = htons( port );
			return true;
		}
		return false;
	}

public:
	NetworkAddress() {
		Clear();
	}

	bool IsIpV4Address() const { return Family() == AF_INET; }
	bool IsIpV6Address() const { return Family() == AF_INET6; }

	in_port_t Port() const { return htons( u.in4.sin_port ); }
	inline sa_family_t Family() const { return u.in4.sin_family; }

	const sockaddr_in *AsIpV4Sockaddr() const { return IsIpV4Address() ? &u.in4 : nullptr; }
	const sockaddr_in6 *AsIpV6Sockaddr() const { return IsIpV6Address() ? &u.in6 : nullptr; }

	sockaddr *AsGenericSockaddr() { return (sockaddr *)&u; }
	const sockaddr *AsGenericSockaddr() const { return (const sockaddr *)&u; }

	bool operator==( const NetworkAddress &that ) const {
		if( this->Port() != that.Port() ) {
			return false;
		}

		if( this->Family() == AF_INET ) {
			if( that.Family() != AF_INET ) {
				return false;
			}

			return this->u.in4.sin_addr.s_addr == that.u.in4.sin_addr.s_addr;
		}

		if( this->Family() == AF_UNSPEC ) {
			return that.Family() == AF_UNSPEC;
		}

		return !memcmp( &this->u.in6.sin6_addr, &that.u.in6.sin6_addr, sizeof( u.in6.sin6_addr ) );
	}

	bool operator!=( const NetworkAddress &that ) const { return !( *this == that ); }

	void Clear() {
		memset( this, 0, sizeof( NetworkAddress ) );
		u.in4.sin_family = AF_UNSPEC;
	}
};

class UnresolvedAddress
{
	friend class AddressResolver;

	NetworkAddress address;
	bool hasParsingErrors;
	bool isResolved;

public:
	UnresolvedAddress( const char *string );

	bool IsValidAsString() const { return !hasParsingErrors; }

	inline bool IsResolved() const { return isResolved; }

	NetworkAddress ToResolvedAddress() const;
};

#endif
