// MD5 and SHA-256 through Windows' own cryptography API (no third-party code).
#pragma once

#include <cstddef>
#include <cstdint>

// MD5 is only used to check the CD's archives, which store an MD5 of every file.
void Md5(const void* data, size_t size, uint8_t digest[16]);
void Sha256(const void* data, size_t size, uint8_t digest[32]);

// MD5 of data that arrives in pieces, e.g. a large file being copied.
class Md5Hasher
{
public:
    Md5Hasher();
    ~Md5Hasher();
    Md5Hasher(const Md5Hasher&) = delete;
    Md5Hasher& operator=(const Md5Hasher&) = delete;

    void Update(const void* data, size_t size);
    void Finish(uint8_t digest[16]);

private:
    void* m_provider = nullptr;
    void* m_hash = nullptr;
};
