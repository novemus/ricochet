#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <repo.h>

namespace ricochet {

repository::repository(const std::filesystem::path& repo)
    : m_repo(repo)
{
    update_cache_incremental();
}

bool repository::is_certificate_allowed(X509* cert) const
{
    if (!cert)
        return false;

    update_cache_incremental();

    std::string hash = get_certificate_hash_from_x509(cert);

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

    std::string hash = get_certificate_hash_from_x509(cert.get());
    return std::make_pair(hash, std::move(cert));
}

void repository::update_cache_incremental() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

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
                m_cache.emplace(certificate { hash, file, std::move(cert), time });
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error loading certificate " << file << ": " << e.what() << std::endl;
                continue;
            }
        }
    }
}

std::string repository::get_certificate_hash_from_x509(X509* cert) const
{
    unsigned char* data = nullptr;
    int length = i2d_X509(cert, &data);

    if (length < 0)
    {
        if (data)
            ::free(data);
        return "";
    }

    std::unique_ptr<unsigned char, decltype(&::free)> ptr(data, &::free);

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(ptr.get(), length, hash);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
    {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return ss.str();
}

std::set<std::filesystem::path> repository::collect_current_certificate_files() const
{
    std::set<std::filesystem::path> files;

    for (const auto& owner : std::filesystem::directory_iterator(m_repo))
    {
        if (!owner.is_directory())
            continue;

        for (const auto& host : std::filesystem::directory_iterator(owner))
        {
            if (!host.is_directory())
                continue;

            std::filesystem::path cert_file_path = host.path() / "cert.crt";
            if (!std::filesystem::exists(cert_file_path))
            {
                cert_file_path = host.path() / "cert.pem";
                if (!std::filesystem::exists(cert_file_path))
                    continue;
            }

            files.insert(std::filesystem::absolute(cert_file_path));
        }
    }

    return files;
}

} // namespace ricochet
