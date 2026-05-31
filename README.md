# Ricochet

[Ricochet](https://github.com/novemus/ricochet) is a relay server and C++ client library for bridging UDP/TCP applications that cannot connect directly due to strict firewall rules or NAT constraints.

It works similarly to a TURN relay, but unlike TURN it is not tied to multimedia protocols — it can relay raw UDP or TCP data transparently for any type of application.

A typical scenario is the connection of peers when one of them located behind a symmetric NAT. For success, in that case another peer must have no NAT or Full Cone NAT filtering. If these conditions are not met, then you need a relay.

## How It Works

It is assumed that peers coordinate connection via some rendezvous service and a relay initiator implements `Ricochet` protocol. There are three steps to make relay:

1. The relay initiator requests a relay endpoint from the server, specifying the desired protocol. The server returns the assigned address and port.
2. The relay initiator negotiates peer roles (`client` or `server`) with peers and passes them the relay endpoint via a rendezvous service.
3. The relay initiator forwards to the server the description of peers and triggers their connection. The relay accepts the `client` peer(s), then connects the `server` peer, if there is one, and begins data forwarding.

Peer with bad NAT must implement `client` role. Endpoint of the `server` peer must be defined, but the `client` peer endpoint is used as an accept filter and can be defined partially or undefined at all.

## Features

- **Full protocol matrix:** UDP4, TCP4, UDP6, TCP6
- **Security:** mutual TLS (mTLS) with certificate-based authentication
- **Flexible limits:** per-client session cap and global session limit
- **Idle timeout:** automatic cleanup of idle sessions
- **Multi-threading:** automatic scaling to the number of CPU cores

## Build

The project depends on [Boost](https://github.com/boostorg/boost) and [OpenSSL](https://github.com/openssl/openssl). You can download [prebuilt packages](https://github.com/novemus/ricochet/releases) or build from source:

```console
$ cd ~
$ git clone https://github.com/novemus/ricochet.git
$ cd ~/ricochet
$ cmake -B ./build -DCMAKE_BUILD_TYPE=Release [-DBUILD_SHARED_LIBS=ON] [-DBOOST_ROOT=...] [-DOPENSSL_ROOT_DIR=...]
$ cmake --build ./build --target all
$ cmake --build ./build --target install
```

## Usage

### Certificate Setup

Server requires mTLS. You can rely on your own PKI infrastructure to issue certificates, or use preshared self-signed certificates. In the latter case, the client can use the server certificate as a CA, and the server must store client certificates in a local repository with the following structure:

```console
repo/
  ├── owner1/
  │   ├── host1/
  │   │   └── cert.pem
  │   └── host2/
  │       └── cert.crt
  └── owner2/
      └── host1/
          └── cert.pem
```

Accepted file extensions: `.pem` and `.crt`.

### Running the Server

```console
$ ./ricochet --help
$ ./ricochet --bind-addr=0.0.0.0 --bind-port=443 --cert=server.pem --key=server.key --repo=.
```

Server options:

| Option | Default | Description |
|--------|---------|-------------|
| `--address` | `0.0.0.0` | Listen address |
| `--port` | `443` | Listen port |
| `--cert` | `server.pem` | Server certificate |
| `--key` | `server.key` | Server private key |
| `--ca` | — | CA for client verification (optional) |
| `--repo` | `.` | Client certificate repository |
| `--idle` | `180` | Idle session timeout in seconds |
| `--quota` | `10` | Maximum sessions per client |
| `--limit` | `100` | Maximum concurrent sessions |
| `--report` | `info` | Logging level (trace, debug, info, warning, error, fatal) |
| `--journal` | — | Path to log file (optional) |

### Client API

**Interface:**

```cpp
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <filesystem>
#include "proto.h"

namespace ricochet {

struct agent
{
    virtual ~agent() {}
    virtual void assign_relay(boost::asio::yield_context yield, protocol proto, endpoint& relay) = 0;
    virtual void launch_relay(boost::asio::yield_context yield, const peer& red, const peer& blue) = 0;
};

std::shared_ptr<agent> create_agent(const boost::asio::ip::tcp::endpoint& server,
                                    const std::filesystem::path& cert,
                                    const std::filesystem::path& key,
                                    const std::filesystem::path& ca);
}
```

**Workflow:**

1. Call `assign_relay` with the desired protocol. The server will return an allocated relay endpoint or throws an exception (`unavailable_proto`, `limit_reached`).
2. Define peer roles (`client` or `server`) and forward them with the relay endpoint to both peers via a rendezvous service.
3. Call `launch_relay` with the description of both peers. The relay will accept `client` peer(s) and connect to the `server` peer, if there is one, then start transmitting data.

See the header files [agent.h](src/agent.h) and [proto.h](src/proto.h) for details.

## License

Licensed under the Apache License 2.0. You are free to use it for commercial and non-commercial purposes as long as you fulfill its conditions. See [LICENSE.txt](LICENSE.txt) for details.

## Copyright

Copyright © 2026 Novemus Band. All Rights Reserved.