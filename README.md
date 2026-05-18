# Ricochet

[Ricochet](https://github.com/novemus/ricochet) is a relay server and C++ client library for bridging UDP/TCP applications that cannot connect directly due to strict firewall rules or NAT constraints.

It works similarly to a TURN relay, but unlike TURN it is not tied to multimedia protocols — it can relay raw UDP or TCP data for any type of application.

A typical scenario is the connection of peers when one of them located behind a symmetric NAT. For success, in that case another peer must have no NAT or Full Cone NAT filtering. If these conditions are not met, then you need a relay.

## How It Works

Peers must coordinate connection via some rendezvous service. Peer with bad NAT must act as a `client`, another one can be `server` or `client`. No matter who acts as a relay initiator.

1. **Relay allocation** — the relay initiator requests a relay endpoint from the server, specifying the desired protocol (UDP/TCP, IPv4/IPv6). The server returns the allocated address and port.
2. **Peer coordination** — relay initiator negotiates connection roles and startup synchronization with peers and passes them the relay endpoint via a rendezvous service.
3. **Relay launch** — the relay initiator forwards to the server the description (endpoints and roles) of both peers. The relay then accepts connections from the `client` peer, attempts to connect to the `server` peer, and begins transparent data forwarding once both sides are connected.

Endpoint of the `server` peer must be defined, but the `client` peer endpoint used by the relay as an accept filter and can be defined partially or undefined at all.

## Features

- **Full protocol matrix:** UDP4, TCP4, UDP6, TCP6
- **Security:** mutual TLS (mTLS) with certificate-based authentication
- **Flexible limits:** per-client session cap and global session limit
- **Idle timeout:** automatic cleanup of inactive sessions
- **Logging:** configurable verbosity (trace, debug, info, warning, error, fatal), optional log file output
- **Multi-threading:** automatic scaling to the number of CPU cores

## Build

The project depends on [Boost](https://github.com/boostorg/boost) and [OpenSSL](https://github.com/openssl/openssl). You can download [prebuilt packages for Debian and Windows](https://github.com/novemus/ricochet/releases) or build from source:

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

The server uses mTLS. You can rely on your own PKI infrastructure to issue certificates, or use preshared self-signed certificates. In the latter case, the client must use the server certificate as a CA, and the server must store client certificates in a local repository with the following structure:

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
$ ./ricochet
```

Server options:

| Option | Default | Description |
|--------|---------|-------------|
| `--bind-addr` | `0.0.0.0` | IP address to bind the server |
| `--bind-port` | `4433` | Port to bind the server |
| `--cert-file` | `server.pem` | Path to the server SSL certificate |
| `--key-file` | `server.key` | Path to the SSL private key |
| `--ca-file` | `ca.pem` | Path to the CA certificate |
| `--repo-path` | `.` | Path to the client certificate repository |
| `--idle-time` | `300` | Idle session timeout in seconds |
| `--client-limit` | `10` | Maximum sessions per client |
| `--total-limit` | `100` | Maximum concurrent sessions |
| `--log-level` | `info` | Logging level (trace, debug, info, warning, error, fatal) |
| `--log-file` | — | Path to log file (optional) |

### Client API

Brief interface overview:

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

1. Call `assign_relay` with the desired protocol. The server returns an allocated relay endpoint or throws an exception (`unavailable_proto`, `limit_reached`).
2. Define peer roles and forward them with the realay endpoint to both peers via a rendezvous service. If some peer uses the `server` role, it must start accepting connections on the relay endpoint beforehand.
3. Call `launch_relay` with the description of both peers. The relay starts accepting connections from `client` peers, makes several attempts to connect to the `server` peer, and begins transparent data forwarding once the connection is established.

See the header files [agent.h](src/agent.h) and [proto.h](src/proto.h) for details.

## License

Licensed under the Apache License 2.0. You are free to use it for commercial and non-commercial purposes as long as you fulfill its conditions. See [LICENSE.txt](LICENSE.txt) for details.

## Copyright

Copyright © 2026 Novemus Band. All Rights Reserved.