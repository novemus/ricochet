/*
 * Copyright (c) 2026 Novemus Band. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 */

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>
#include <fstream>
#include <random>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include "repo.h"

namespace fs = std::filesystem;

// Helper function to create a valid test certificate
std::string get_valid_cert_content()
{
    return R"(-----BEGIN CERTIFICATE-----
MIIDFzCCAf+gAwIBAgIUWfHezhJRgHCXoNpO1EHBldxeTi0wDQYJKoZIhvcNAQEL
BQAwGzEZMBcGA1UEAwwQdGVzdC5leGFtcGxlLmNvbTAeFw0yNjA1MDYwOTIwMzVa
Fw0yNzA1MDYwOTIwMzVaMBsxGTAXBgNVBAMMEHRlc3QuZXhhbXBsZS5jb20wggEi
MA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDn5Xr8hEg92mnqMlXbCuS4thf3
VMQ+rn9VC+oszMeuBTszdGn0j9b0DQlmwd451K1Y0xh0SJxo7XxXYjnCejvovTbK
12currE7uJN0+7z+AHDDGZ0U5kk29XKQzdpzalani3j1Vi5XUrkrYTXuZge12Yc+
D39btEk/ZERIMN0HKeQdcvNXzGGM3GknaMhKXhJdwb3usbNeooVcEv1+B2WeQrCn
x6kKjAMLdfeSr4hRrYoB6VsZ2HsM16rO3Z3v+TqUB/p9jU/bHbrBEOphpz30bCcL
AKCxCDY2pc4yAALSq203zAV+00NRt9Itd/e19n6FPwzkZ9At8oi34P+DiX5ZAgMB
AAGjUzBRMB0GA1UdDgQWBBSEAH9z8SurCSNuqsPf8jSnSx8FJzAfBgNVHSMEGDAW
gBSEAH9z8SurCSNuqsPf8jSnSx8FJzAPBgNVHRMBAf8EBTADAQH/MA0GCSqGSIb3
DQEBCwUAA4IBAQBM2r8wcpMx207EXZd2sm4krG2BSz+uWQT/9ilb2i5soRw198jY
61yj4Gt9U9ZUUGPac2E5EqPPYu/izqDEWBEVYtjdV72MXemnrh+r+bJcGYZGAr2U
iLV3XUo2hA8Sw5sglphIF+iJyKcWwErv9SfppKPOhvgrmmE1SqMULmOWSVoacS45
2MmNNczzN3O8a+Eoq1vfROGytkXGY0St7oX9/9umxspDxgzc7ITg0Hd2xzFoTQfC
Ab5Sk/LYJs5S9uhks609xPg0391AlEtOUD1GGIf52ClCdb3wvroxKk+ov6jKjPus
fsKtUIKPUPjXt/exmnkZElwYqR7GlycsCmP9
-----END CERTIFICATE-----)";
}

// Helper function to create a bad test certificate
std::string get_wrong_cert_content()
{
    return R"(-----BEGIN CERTIFICATE-----
MIIDFzCCAf+gAwIBAgIUEL6q9D7z8W7jL3Y2X4Q5Z9K8R2MwDQYJKoZIhvcNAQEL
BQAwGzEZMBcGA1UEAwwQdGVzdDIuZXhhbXBsZS5jb20wHhcNMjYwNTA2MDkyMDM1
WhcNMjcwNTA2MDkyMDM1WjAbMRkwFwYDVQQDDBR0ZXN0Mi5leGFtcGxlLmNvbTCB
njANBgkqhkiG9w0BAQEFAAOBjQAwgYkCgYEAuX+2R8L6J3Q4K7mB1Q5Z9K8R2M7
B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W
7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q
5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7
B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W
7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q
5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7
B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W
7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q
5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7B6q9D7z8W7jL3Y2X4Q5Z9K8R2M7
-----END CERTIFICATE-----)";
}

// Helper function to load X509 certificate from string
ricochet::X509Ptr load_cert_from_string(const std::string& cert_content)
{
    ricochet::BIOPtr bio(BIO_new_mem_buf(cert_content.c_str(), cert_content.length()));
    ricochet::X509Ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
    return cert;
}

struct repository_test_fixture
{
    fs::path m_repo;

    repository_test_fixture()
    {
        m_repo = fs::temp_directory_path() / ("ricochet_repo_test_" + std::to_string(std::random_device{}())) / "repo";
        fs::create_directories(m_repo);
    }

    ~repository_test_fixture()
    {
        if (fs::exists(m_repo.parent_path())) {
            fs::remove_all(m_repo.parent_path());
        }
    }

    void create_owner_host_structure(const std::string& owner, const std::string& host, 
                                   const std::string& cert_content, const std::string& filename = "cert.crt")
    {
        fs::create_directories(m_repo / owner / host);
        
        fs::path cert_file = m_repo / owner / host / filename;
        std::ofstream file(cert_file.string());
        file << cert_content;
        file.close();
    }

    fs::path get_cert_path(const std::string& owner, const std::string& host, const std::string& filename = "cert.crt")
    {
        return m_repo / owner / host / filename;
    }
};

BOOST_FIXTURE_TEST_SUITE(repository_tests, repository_test_fixture)

BOOST_AUTO_TEST_CASE(get_certificate_hash_null)
{
    std::string null_hash = ricochet::repository::get_certificate_hash(nullptr);
    BOOST_TEST(null_hash.empty());
}

BOOST_AUTO_TEST_CASE(get_certificate_hash_valid)
{
    std::string cert_content = get_valid_cert_content();
    ricochet::X509Ptr cert = load_cert_from_string(cert_content);
    BOOST_REQUIRE(cert != nullptr);
    
    std::string hash = ricochet::repository::get_certificate_hash(cert.get());
    BOOST_TEST(!hash.empty());
    BOOST_TEST(hash.length() == 64); // SHA256 hex length
    
    BOOST_TEST(hash == ricochet::repository::get_certificate_hash(cert.get()));
}

BOOST_AUTO_TEST_CASE(empty_repository)
{
    ricochet::repository repo(m_repo);

    BOOST_TEST(!repo.is_certificate_allowed(nullptr));
    BOOST_TEST(fs::is_directory(m_repo));
    BOOST_TEST(fs::is_empty(m_repo));
}

BOOST_AUTO_TEST_CASE(repository_with_correct_structure)
{
    std::string cert1_content = get_valid_cert_content();
    std::string cert2_content = get_wrong_cert_content();

    create_owner_host_structure("owner1", "host1.example.com", cert1_content);
    create_owner_host_structure("owner1", "host2.example.com", cert1_content);
    create_owner_host_structure("owner2", "host3.example.com", cert2_content);

    ricochet::repository repo(m_repo);

    BOOST_TEST(fs::exists(get_cert_path("owner1", "host1.example.com")));
    BOOST_TEST(fs::exists(get_cert_path("owner1", "host2.example.com")));
    BOOST_TEST(fs::exists(get_cert_path("owner2", "host3.example.com")));
}

BOOST_AUTO_TEST_CASE(is_certificate_allowed_with_valid_cert)
{
    std::string cert_content = get_valid_cert_content();
    create_owner_host_structure("testowner", "testhost.com", cert_content);

    ricochet::repository repo(m_repo);
    ricochet::X509Ptr cert = load_cert_from_string(cert_content);

    BOOST_REQUIRE(cert != nullptr);
    BOOST_TEST(repo.is_certificate_allowed(cert.get()));
}

BOOST_AUTO_TEST_CASE(is_certificate_allowed_with_wrong_cert)
{
    std::string repo_cert_content = get_valid_cert_content();
    create_owner_host_structure("owner", "host.com", repo_cert_content);

    ricochet::repository repo(m_repo);
    std::string wrong_cert_content = get_wrong_cert_content();
    ricochet::X509Ptr wrong_cert = load_cert_from_string(wrong_cert_content);

    BOOST_REQUIRE(wrong_cert == nullptr);
}

BOOST_AUTO_TEST_CASE(certificate_hash_consistency)
{
    std::string cert_content = get_valid_cert_content();
    ricochet::X509Ptr cert1 = load_cert_from_string(cert_content);
    ricochet::X509Ptr cert2 = load_cert_from_string(cert_content);
    
    BOOST_REQUIRE(cert1 != nullptr);
    BOOST_REQUIRE(cert2 != nullptr);
    
    std::string hash1 = ricochet::repository::get_certificate_hash(cert1.get());
    std::string hash2 = ricochet::repository::get_certificate_hash(cert2.get());
    
    BOOST_TEST(hash1 == hash2);
}

BOOST_AUTO_TEST_CASE(cert_pem_extension)
{
    std::string cert_content = get_valid_cert_content();
    create_owner_host_structure("owner", "host.com", cert_content, "cert.pem");

    ricochet::repository repo(m_repo);
    BOOST_TEST(fs::exists(get_cert_path("owner", "host.com", "cert.pem")));

    ricochet::X509Ptr cert = load_cert_from_string(cert_content);
    BOOST_REQUIRE(cert != nullptr);

    BOOST_TEST(repo.is_certificate_allowed(cert.get()));
}

BOOST_AUTO_TEST_SUITE_END()
