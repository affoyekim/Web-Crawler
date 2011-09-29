#ifndef _SIMPLESOCKET_H_
#define _SIMPLESOCKET_H_

#include <stdlib.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <cassert>
#include <string>
#include <string.h>		// for memset()
#include <sstream>
#include <errno.h>
#include <iostream>
#include <errno.h>
#include <sys/select.h>

using namespace std;

enum {TIMEOUT_ERR = -2};

enum HttpMessageType
{
  GET  = 0x0,
  POST = 0x1,
  INVALID = 0x2
};

struct HttpMessage
{
  HttpMessage(HttpMessageType in_type, string in_resource, string in_httpVersion) : 
              MessageType(in_type),
              Resource(in_resource),
              HttpVersion(in_httpVersion)
  { }
  HttpMessageType MessageType;
  string Resource;
  string HttpVersion;
};

class simplesocket {
	private:

	struct addrinfo * _addresses;
	int _socketfd;
	bool _nameResolved;
	bool _seenEof;
	int _port;
	const string _hostname;
	const string _name;
	ostringstream _str;
	int _timeout;
	bool _debug;
	
	string itos (int v) {
		stringstream str;
		str << v;
		return str.str();
	}

public:
	simplesocket (const char * str, int port, int timeout = -1, bool debug = false)
		: _nameResolved (false),
		_seenEof(false),
		_port (port),
		_hostname (str),
		_name (_hostname + string(":") + itos(_port)),
		_timeout (timeout),
		_debug(debug){}
		
		bool resolved (void) const;
		void resolve (struct addrinfo * hint);
		
		
		void close();
		
		virtual ~simplesocket (void);
		
		const char * name (void) const;
		const char * hostname (void) const;
		int port (void) const;
		int descriptorReady (int fd, int timeout);
		ssize_t recvNBytes(void* data, int size, bool lessOk=false);
		ssize_t sendNBytes(unsigned char* data, int size, bool lessOk=false);
		bool seenEof();
		int write (const char * buf, size_t len);
		int read (char * buf, int len);
		bool connect (void);
		void serve (void);
		simplesocket * accept (void);
		HttpMessage parseRequest(const string& in_message);
		void SendHttpMessage(char* in_data, size_t length);
		HttpMessage ReceiveHttpMessage();
		void setTimeout(int timeout);
};

class clientsocket : public simplesocket {
public:
	clientsocket (const char * str, int port, int timeout = -1, bool debug = false)
		: simplesocket (str, port, timeout, debug)
	{
		resolve (NULL);
	}
		
private:
	// Clients can't serve or accept connections.
	void serve (void);
	simplesocket * accept (void);
};


class serversocket : public simplesocket {
public:
	serversocket (int port, int timeout = -1, bool debug = false)
		: simplesocket ("localhost", port, timeout, debug)
	{
		struct addrinfo hint;
		memset ((void *) &hint, 0, sizeof(hint));
		hint.ai_flags = AI_PASSIVE;
		resolve (&hint);
		serve();
	}
		
		
	private:
	// Servers can't make new connections (out).
	bool connect (void);
};

#ifndef MSG_NOSIGNAL
/* Operating systems which have SO_NOSIGPIPE but not MSG_NOSIGNAL */
#if defined (__FreeBSD__) || defined (__OpenBSD__) || defined(__APPLE__)
#define MSG_NOSIGNAL SO_NOSIGPIPE
#else
#error Your OS doesnt support MSG_NOSIGNAL or SO_NOSIGPIPE
#endif
#endif

#endif
