#include "relay.h"

namespace ricochet {

protocol tcp_relay::get_protocol() const
{
    return m_server.is_open() && m_server.local_endpoint().address().is_v6() 
           ? protocol::tcp6 : protocol::tcp4;
}

protocol udp_relay::get_protocol() const
{
    return m_socket.is_open() && m_socket.local_endpoint().address().is_v6() 
           ? protocol::udp6 : protocol::udp4;
}

}