/*
 * Copyright (c) 2026 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include "repo.h"
#include "logging.h"

namespace ricochet {

repository::repository(const std::filesystem::path& repo)
    : m_repo(repo)
{
    update_cache_incremental();
}

bool repository::is_certificate_allowed(X509* cert) const
{
    if (!cert || !std::filesystem::exists(m_repo))
        return false;

    update_cache_incremental();

    std::string hash = get_certificate_hash(cert);

    std::lock_guard<std::mutex> lock(m_mutex);
    const auto& hashs = m_cache.get<by_hash>();
    auto it = hashs.find(hash);
    return it != hashs.end() && X509_cmp(cert, it->cert.get()) == 0;
}

std::string repository::get_certificate_hash(X509* cert)
{
    if (!cert)
        return "";

    unsigned char* raw = nullptr;
    int length = i2d_X509(cert, &raw);

    if (length < 0)
    {
        if (raw)
            ::free(raw);
        return "";
    }

    std::unique_ptr<unsigned char, decltype(&::free)> data(raw, &::free);

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data.get(), length, hash);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return ss.str();
}

std::string repository::get_certificate_cn(X509* cert)
{
    if (!cert)
        return "";

    X509_NAME* subject = X509_get_subject_name(cert);
    if (!subject)
        return "";

    int len = X509_NAME_get_text_by_NID(subject, NID_commonName, nullptr, 0);
    if (len <= 0)
        return "";

    std::string cn(len + 1, '\0');
    if (X509_NAME_get_text_by_NID(subject, NID_commonName, cn.data(), len + 1) <= 0)
        return "";

    cn.resize(len);
    return cn;
}

std::pair<std::string, X509Ptr> repository::load_certificate_file(const std::filesystem::path& path) const
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot read certificate file: " + path.string());

    std::string cert_content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

    BIOPtr bio(BIO_new_mem_buf(cert_content.c_str(), cert_content.length()));
    X509Ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));

    if (!cert)
        throw std::runtime_error("Failed to parse certificate: " + path.string());

    std::string hash = get_certificate_hash(cert.get());
    return std::make_pair(hash, std::move(cert));
}

void repository::update_cache_incremental() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!std::filesystem::exists(m_repo))
        return;

    auto files = collect_current_certificate_files();

    auto& paths = m_cache.get<by_path>();
    for (auto it = paths.begin(); it != paths.end();)
    {
        if (files.find(it->path) == files.end())
        {
            it = paths.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (const auto& file : files)
    {
        auto time = std::filesystem::last_write_time(file);

        auto& paths = m_cache.get<by_path>();
        auto pi = paths.find(file);

        if (pi == paths.end() || pi->time != time)
        {
            try
            {
                if (pi != paths.end())
                    paths.erase(pi);

                auto [hash, cert] = load_certificate_file(file);

                _inf_ << "Loaded certificate for " << file.parent_path().parent_path().filename().string() << "/" << file.parent_path().filename().string() 
                      << ", CN=" << get_certificate_cn(cert.get()) << ", SHA256=" << hash;

                m_cache.emplace(certificate { hash, file, std::move(cert), time });
            }
            catch (const std::exception& e)
            {
                _wrn_ << "Error loading certificate " << file << ": " << e.what();
                continue;
            }
        }
    }
}

std::set<std::filesystem::path> repository::collect_current_certificate_files() const
{
    std::set<std::filesystem::path> files;

    if (std::filesystem::exists(m_repo))
    {
        for (const auto& owner : std::filesystem::directory_iterator(m_repo))
        {
            if (!owner.is_directory())
                continue;

            for (const auto& host : std::filesystem::directory_iterator(owner))
            {
                if (!host.is_directory())
                    continue;

                for (const auto& file : std::filesystem::directory_iterator(host))
                {
                    if (file.path().extension() == ".crt" || file.path().extension() == ".pem" )
                    {
                        files.insert(std::filesystem::absolute(file.path()));
                    }
                }
            }
        }
    }
    return files;
}

} // namespace ricochet
