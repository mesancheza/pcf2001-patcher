#include "hashes.h"

#include <windows.h>
#include <bcrypt.h>

namespace
{
void HashData(BCRYPT_HASH_HANDLE hash, const void* data, size_t size)
{
    // BCryptHashData takes at most 4 GB per call.
    const auto* bytes = static_cast<const UCHAR*>(data);
    while (size > 0)
    {
        const ULONG chunk = size > 0x40000000 ? 0x40000000 : static_cast<ULONG>(size);
        BCryptHashData(hash, const_cast<PUCHAR>(bytes), chunk, 0);
        bytes += chunk;
        size -= chunk;
    }
}

void Hash(LPCWSTR algorithm, const void* data, size_t size, uint8_t* digest, ULONG digestSize)
{
    BCRYPT_ALG_HANDLE provider = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&provider, algorithm, nullptr, 0) == 0 &&
        BCryptCreateHash(provider, &hash, nullptr, 0, nullptr, 0, 0) == 0)
    {
        HashData(hash, data, size);
        BCryptFinishHash(hash, digest, digestSize, 0);
    }
    if (hash)
        BCryptDestroyHash(hash);
    if (provider)
        BCryptCloseAlgorithmProvider(provider, 0);
}
}  // namespace

void Md5(const void* data, size_t size, uint8_t digest[16])
{
    Hash(BCRYPT_MD5_ALGORITHM, data, size, digest, 16);
}

void Sha256(const void* data, size_t size, uint8_t digest[32])
{
    Hash(BCRYPT_SHA256_ALGORITHM, data, size, digest, 32);
}

Md5Hasher::Md5Hasher()
{
    BCRYPT_ALG_HANDLE provider = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&provider, BCRYPT_MD5_ALGORITHM, nullptr, 0) == 0)
        BCryptCreateHash(provider, &hash, nullptr, 0, nullptr, 0, 0);
    m_provider = provider;
    m_hash = hash;
}

Md5Hasher::~Md5Hasher()
{
    if (m_hash)
        BCryptDestroyHash(m_hash);
    if (m_provider)
        BCryptCloseAlgorithmProvider(m_provider, 0);
}

void Md5Hasher::Update(const void* data, size_t size)
{
    if (m_hash)
        HashData(m_hash, data, size);
}

void Md5Hasher::Finish(uint8_t digest[16])
{
    if (m_hash)
        BCryptFinishHash(m_hash, digest, 16, 0);
}
