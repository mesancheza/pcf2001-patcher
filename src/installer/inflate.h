// Decompressor for raw DEFLATE data (RFC 1951), the compression InstallShield uses inside its .cab archives.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Appends the decompressed data to `out`. Returns false if the input is not valid DEFLATE data; `out` may then hold
// part of the output. Input that continues after the final block is ignored.
bool InflateRaw(const uint8_t* in, size_t size, std::vector<uint8_t>& out);
