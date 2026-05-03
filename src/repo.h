#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <set>
#include <boost/asio.hpp>
#include <openssl/x509.h>
#include <openssl/bio.h>
#include <openssl/sha.h>
#include <openssl/pem.h>

namespace ricochet {

// Custom deleter for X509 certificates
struct X509Deleter
{
    void operator()(X509* cert) const noexcept
    {
        if (cert)
            X509_free(cert);
    }
};

// Custom deleter for BIO structures
struct BIODeleter
{
    void operator()(BIO* bio) const noexcept
    {
        if (bio)
            BIO_free(bio);
    }
};

using X509Ptr = std::unique_ptr<X509, X509Deleter>;
using BIOPtr = std::unique_ptr<BIO, BIODeleter>;

class repository
{
    std::filesystem::path m_repo;
    mutable std::map<std::string, X509Ptr> m_cache;
    mutable std::map<std::string, std::filesystem::file_time_type> m_times;
    mutable std::mutex m_mutex;

    void update_cache_incremental() const;
    std::string get_certificate_hash_from_x509(X509* cert) const;
    std::set<std::filesystem::path> collect_current_certificate_files() const;
    std::pair<std::string, X509Ptr> load_certificate_file(const std::filesystem::path& file_path) const;

public:

    explicit repository(const std::filesystem::path& repo);
    ~repository() = default;

    bool is_certificate_allowed(X509* cert) const;
    static std::string get_certificate_hash(X509* cert);
};

} // namespace ricochet
