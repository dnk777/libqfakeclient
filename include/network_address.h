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

	void SetFromIpV4Data( const uint8_t *addressBytes, const uint8_t *portBytes ) {
		memset( &u.in4, 0, sizeof( u.in4 ) );
		memcpy( &u.in4.sin_addr, addressBytes, 4 );
		memcpy( &u.in4.sin_port, portBytes, 2 );
		u.in4.sin_family = AF_INET;
	}
	void SetFromIpV6Data( const uint8_t *addressBytes, const uint8_t *portBytes ) {
		memset( &u.in6, 0, sizeof( u.in6 ) );
		memcpy( &u.in6.sin6_addr, addressBytes, 16 );
		memcpy( &u.in6.sin6_port, portBytes, 2 );
		u.in6.sin6_family = AF_INET6;
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

	uint32_t Hash() const {
		if( this->Family() == AF_INET ) {
			return HashForIpV4Data( (uint8_t *)&u.in4.sin_addr.s_addr, (uint8_t *)&u.in4.sin_port );
		}

		if( this->Family() == AF_INET6 ) {
			return HashForIpV6Data( (uint8_t *)&u.in6.sin6_addr, (uint8_t *)&u.in6.sin6_port );
		}
		return 0;
	}

	static uint32_t HashForIpV4Data( const uint8_t *addressData, const uint8_t *portData ) {
		const uint8_t *data = addressData;
		uint32_t result = ~( 0u ^ ( portData[0] | ( portData[1] << 24 ) ) );

		result = result * 17 + ( ( data[0] << 24 ) | ( data[1] << 16 ) | ( data[2] << 8 ) | data[0] );
		return result;
	}

	static uint32_t HashForIpV6Data( const uint8_t *addressData, const uint8_t *portData ) {
		uint32_t result = ~( 0u  ^ ( portData[0] | ( portData[1] << 24 ) ) );
		const uint8_t *data = addressData;

		// Not sure about alignment of IP v6 data
		for( unsigned i = 0; i < 16; i += 4 ) {
			result *= 17;
			result += ( ( data[i + 0] << 24 ) | ( data[i + 1] << 16 ) | ( data[i + 2] << 8 ) | data[i + 3] );
		}
		return result;
	}

	bool MatchesIpV4Data( const uint8_t *addressData, const uint8_t *portData ) const {
		if( !IsIpV4Address() ) {
			return false;
		}
		return !memcmp( &u.in4.sin_addr.s_addr, addressData, 4 ) && !memcmp( &u.in4.sin_port, portData, 2 );
	}

	bool MatchesIpV6Data( const uint8_t *addressData, const uint8_t *portData ) const {
		if( !IsIpV6Address() ) {
			return false;
		}
		return !memcmp( &u.in6.sin6_addr, addressData, 16 ) && !memcmp( &u.in6.sin6_port, portData, 2 );
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
