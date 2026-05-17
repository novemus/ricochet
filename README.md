# README

The [Ricochet](https://github.com/novemus/ricochet) app provides the ability to connect network UDP/TCP applications which cannot connect each other directly due to the strict firewall or NAT rules. It works like the TURN relay, but can be used for any apps based on UDP/TCP, not only for multimedia ones. After relay endpoint is negotiated, the peers connect each other transparently via the relay, which simply retransmit raw UDP/TCP data. The project consists of relay server and simple C++ client library. Of couse you need to use the client with some signaling service to synchronize the peers.

## Build

You can download [prebuild packages](https://github.com/novemus/ricochet/releases) for Debian and Windows platforms.

The project depends on [boost](https://github.com/boostorg/boost), [openssl](https://github.com/openssl/openssl) libraries. Clone the repository, then configure and build project.

```console
$ cd ~
$ git clone https://github.com/novemus/ricochet.git
$ cd ~/ricochet
$ cmake -B ./build -DCMAKE_BUILD_TYPE=Release [-DBUILD_SHARED_LIBS=ON] [-DBOOST_ROOT=...] [-DOPENSSL_ROOT_DIR=...]
$ cmake --build ./build --target all
$ cmake --build ./build --target install
```

## Using

The server uses SSL protocol and mTLS authentication. So you need to use some CA infrastructue to issue SSL certificates. Alternatively you can use preshared self-signed SSL certificates. In second case your client must use server certificate as the CA and server must store client certificates in the local repository. This is the tree of catalogs with the following structure:

```console
repo
  |_ owner1
      |_ host1
          |_ cert.pem
      |_ host2
          |_ cert.crt
  |_ owner2
      |_ host1
          |_ cert.pem
```

Certificates must have extension *.pem* or *.crt*

Command to run server:

```console
$ ./ricochet
```

Server options:

`--bind-addr arg (=0.0.0.0)` - IP address to bind the server

`--bind-port arg (=4433)` - port to bind the server

`--cert-file arg (=server.pem)` - path to the server SSL certificate

`--key-file arg (=server.key)` - path to the server SSL private key

`--ca-file arg (=ca.pem)` - path to the CA certificate

`--repo-path arg (=.)` - path to the client SSL certificate repository

`--idle-time arg (=300)` - idle session timeout in seconds

`--client-limit arg (=10)` - maximum sessions per client

`--total-limit arg (=100)` - maximum sessions count

`--log-level arg (=info)` - logging level (trace, debug, info, warning, error, fatal)

`--log-file arg` - log file path (optional)

Brief description of the client API:

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

To deploy and start relay you should make the following steps. Call *assign_relay* method to allocate relay. It gives protocol argument and returns relay endpoint assigned by the server. It can throw exception if the specified protocol is not supported on the server or session limit is reached. After that pass the endpoint to the peers and define its connection roles. If one of peers use server role, at this point it must start accepting connection from the relay endpoint. Then call *launch_relay* method to start the relay. It gives peer description arguments. Now the client peers should start connection to the relay. The relay starts the accepting connections from the client peers, makes several tries to connect to the server peer and starts data transfering after peers connected.

See the [agent.h](https://github.com/novemus/ricochet/blob/master/src/ricochet/agent.h) and [proto.h](https://github.com/novemus/ricochet/blob/master/src/ricochet/proto.h) headers for details.

## License

The Ricochet is licensed under the Apache License 2.0, which means that you are free to get and use it for commercial and non-commercial purposes as long as you fulfill its conditions. See the LICENSE.txt file for more details.

## Copyright

Copyright © 2026 Novemus Band. All Rights Reserved.
