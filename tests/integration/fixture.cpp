#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <filesystem>
#include <boost/process.hpp>
#include <random>
#include <string>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include "fixture.h"

namespace fs = std::filesystem;
namespace bp = boost::process;

integration_test_fixture::context::context() : repo(fs::temp_directory_path() / ("ricochet_server_test_"  + std::to_string(std::random_device{}())) / "repo")
{
    std::filesystem::path ca_key = repo.parent_path() / "ca.key";
    std::filesystem::path server_csr = repo.parent_path() / "server.csr";
    std::filesystem::path client_csr = repo / "test_client" / "localhost" / "client.csr";

    ca_cert = repo.parent_path() / "ca.crt";
    server_cert = repo.parent_path() / "server.crt";
    server_self_cert = repo.parent_path() / "server.pem";
    server_key = repo.parent_path() / "server.key";
    client_cert = repo / "test_client" / "localhost" / "cert.crt";
    client_self_cert = repo / "test_client" / "localhost" / "cert.pem";
    client_key = repo / "test_client" / "localhost" / "private.key";

    fs::create_directories(repo);
    fs::create_directories(repo / "test_client" / "localhost");

    // Generate CA certificate
    std::string ca_key_cmd = "openssl genrsa -out " + ca_key.string() + " 2048";
    std::string ca_cert_cmd = "openssl req -new -x509 -days 365 -key " + ca_key.string() + 
                           " -out " + ca_cert.string() + 
                           " -subj \"/C=US/ST=CA/L=Test/O=Ricochet/OU=Test/CN=Ricochet Test CA\"";

    // Generate server certificate
    std::string server_key_cmd = "openssl genrsa -out " + server_key.string() + " 2048";
    std::string server_csr_cmd = "openssl req -new -key " + server_key.string() + 
                               " -out " + server_csr.string() + 
                               " -subj \"/C=US/ST=CA/L=Test/O=Ricochet/OU=Test/CN=localhost\"";
    std::string server_cert_cmd = "openssl x509 -req -days 365 -in " + server_csr.string() + 
                                " -CA " + ca_cert.string() + 
                                " -CAkey " + ca_key.string() + 
                                " -CAcreateserial -out " + server_cert.string();
    std::string self_server_cert_cmd = "openssl x509 -req -days 365 -in " + server_csr.string() + 
                                " -signkey " + server_key.string() + 
                                " -out " + server_self_cert.string();

    // Generate client certificate
    std::string client_key_cmd = "openssl genrsa -out " + client_key.string() + " 2048";
    std::string client_csr_cmd = "openssl req -new -key " + client_key.string() + 
                               " -out " + client_csr.string() + 
                               " -subj \"/C=US/ST=CA/L=Test/O=Ricochet/OU=Test/CN=test.client\"";
    std::string client_cert_cmd = "openssl x509 -req -days 365 -in " + client_csr.string() + 
                                " -CA " + ca_cert.string() + 
                                " -CAkey " + ca_key.string() + 
                                " -CAcreateserial -out " + client_cert.string();
    std::string self_client_cert_cmd = "openssl x509 -req -days 365 -in " + client_csr.string() + 
                                " -signkey " + client_key.string() + 
                                " -out " + client_self_cert.string();

    // Execute commands and check return codes
    int result = 0;
    result |= bp::system(ca_key_cmd);
    result |= bp::system(ca_cert_cmd);
    result |= bp::system(server_key_cmd);
    result |= bp::system(server_csr_cmd);
    result |= bp::system(server_cert_cmd);
    result |= bp::system(self_server_cert_cmd);
    result |= bp::system(client_key_cmd);
    result |= bp::system(client_csr_cmd);
    result |= bp::system(client_cert_cmd);
    result |= bp::system(self_client_cert_cmd);

    if (result != 0)
        throw std::runtime_error("Failed to generate test certificates");
}

integration_test_fixture::context::~context()
{
    if (fs::exists(repo.parent_path()))
        fs::remove_all(repo.parent_path());
}

integration_test_fixture::integration_test_fixture()
    : m_work(boost::asio::make_work_guard(m_io))
{
    for (size_t i = 0; i < 4; ++i)
    {
        boost::asio::post(m_pool, [&]()
        {
            m_io.run();
        });
    }
}

integration_test_fixture::~integration_test_fixture()
{
    m_io.stop();
    m_pool.join();
}

std::shared_ptr<ricochet::server> integration_test_fixture::create_server(bool using_ca, size_t client_limit, size_t total_limit)
{
    auto& ctx = get_test_context();

    ricochet::config config;
    config.server_endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 4433);
    config.server_cert = using_ca ? ctx.server_cert : ctx.server_self_cert;
    config.server_key = ctx.server_key;
    config.ca_cert = using_ca ? ctx.ca_cert : std::filesystem::path();
    config.client_repo = ctx.repo;
    config.idle_timeout = boost::posix_time::seconds(2);
    config.client_relay_limit = client_limit;
    config.total_relay_limit = total_limit;

    return std::make_shared<ricochet::server>(m_io, config);
}

std::shared_ptr<ricochet::client> integration_test_fixture::create_client(bool using_ca)
{
    auto& ctx = get_test_context();
    return std::make_shared<ricochet::client>(
        boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 4433),
        using_ca ? ctx.client_cert : ctx.client_self_cert,
        ctx.client_key,
        using_ca ? ctx.ca_cert : ctx.server_self_cert
    );
}

std::shared_ptr<ricochet::agent> integration_test_fixture::create_agent(bool using_ca)
{
    auto& ctx = get_test_context();
    return ricochet::create_agent(
        boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 4433),
        using_ca ? ctx.client_cert : ctx.client_self_cert,
        ctx.client_key,
        using_ca ? ctx.ca_cert : ctx.server_self_cert
    );
}
