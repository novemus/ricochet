#include "relay.h"

namespace ricochet {

void tcp_relay::set_cleaner(std::function<void()> clean)
{
    m_clean = clean;
}

void udp_relay::set_cleaner(std::function<void()> clean)
{
    m_clean = clean;
}

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

tcp_relay::~tcp_relay()
{
    close();
    if (m_clean)
        m_clean();
}

udp_relay::~udp_relay()
{
    close();
    if (m_clean)
        m_clean();
}

}