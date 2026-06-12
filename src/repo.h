/*
 * Copyright (c) 2026 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#pragma once

#include <boost/asio.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <filesystem>
#include <mutex>
#include <memory>
#include <string>
#include <set>
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

// Certificate entry for multi-index container
struct certificate
{
    std::string hash;
    std::filesystem::path path;
    X509Ptr cert;
    std::filesystem::file_time_type time;

    certificate(std::string h, std::filesystem::path p, X509Ptr c, std::filesystem::file_time_type t)
        : hash(std::move(h)), path(std::move(p)), cert(std::move(c)),time(t) {}
};

// Tags for multi-index indices
struct by_hash {};
struct by_path {};

class repository
{
    std::filesystem::path m_repo;

    // Multi-index container for certificates with hash and path indices
    mutable boost::multi_index_container<
        certificate,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<by_hash>,
                boost::multi_index::member<certificate, std::string, &certificate::hash>
            >,
            boost::multi_index::ordered_unique<
                boost::multi_index::tag<by_path>,
                boost::multi_index::member<certificate, std::filesystem::path, &certificate::path>
            >
        >
    > m_cache;

    mutable std::mutex m_mutex;

    void update_cache_incremental() const;
    std::set<std::filesystem::path> collect_current_certificate_files() const;
    std::pair<std::string, X509Ptr> load_certificate_file(const std::filesystem::path& file_path) const;

public:

    explicit repository(const std::filesystem::path& repo);
    ~repository() = default;

    bool is_certificate_allowed(X509* cert) const;
    static std::string get_certificate_hash(X509* cert);
    static std::string get_certificate_cn(X509* cert);
};

} // namespace ricochet
