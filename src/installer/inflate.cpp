#include "inflate.h"

#include <array>

namespace
{
constexpr int kMaxBits = 15;
constexpr int kLiteralLengthCodes = 288;
constexpr int kDistanceCodes = 30;

// Base values and extra bits for length codes 257..285 and distance codes 0..29 (RFC 1951, 3.2.5).
constexpr uint16_t kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                      35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistanceBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
// Order in which the code length code lengths are stored in a dynamic block header.
constexpr uint8_t kCodeLengthOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

class BitReader
{
public:
    BitReader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    // Reads `count` bits (0..16), least significant first. Returns false past the end of the input.
    bool Bits(int count, uint32_t& value)
    {
        while (m_bitCount < count)
        {
            if (m_position >= m_size)
                return false;
            m_buffer |= static_cast<uint32_t>(m_data[m_position++]) << m_bitCount;
            m_bitCount += 8;
        }
        value = m_buffer & ((1u << count) - 1);
        m_buffer >>= count;
        m_bitCount -= count;
        return true;
    }

    void AlignToByte()
    {
        m_buffer = 0;
        m_bitCount = 0;
    }

    bool Bytes(size_t count, std::vector<uint8_t>& out)
    {
        if (m_size - m_position < count)
            return false;
        out.insert(out.end(), m_data + m_position, m_data + m_position + count);
        m_position += count;
        return true;
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    size_t m_position = 0;
    uint32_t m_buffer = 0;
    int m_bitCount = 0;
};

// Canonical Huffman code, decoded one bit at a time: codes of each length are consecutive numbers, and symbols are
// listed in code order.
class Huffman
{
public:
    // Builds the code from the code length of each symbol (0 = unused). Returns false for an over-subscribed code.
    bool Build(const uint8_t* lengths, int symbolCount)
    {
        m_count.fill(0);
        for (int i = 0; i < symbolCount; ++i)
            ++m_count[lengths[i]];
        m_count[0] = 0;
        int left = 1;
        for (int length = 1; length <= kMaxBits; ++length)
        {
            left = (left << 1) - m_count[length];
            if (left < 0)
                return false;
        }
        std::array<uint16_t, kMaxBits + 1> offsets{};
        for (int length = 1; length < kMaxBits; ++length)
            offsets[length + 1] = static_cast<uint16_t>(offsets[length] + m_count[length]);
        for (int i = 0; i < symbolCount; ++i)
        {
            if (lengths[i])
                m_symbols[offsets[lengths[i]]++] = static_cast<uint16_t>(i);
        }
        return true;
    }

    // Returns the next symbol, or -1 for invalid input.
    int Decode(BitReader& bits) const
    {
        int code = 0;   // bits read so far
        int first = 0;  // first code of the current length
        int index = 0;  // index of the first symbol of the current length
        for (int length = 1; length <= kMaxBits; ++length)
        {
            uint32_t bit;
            if (!bits.Bits(1, bit))
                return -1;
            code |= static_cast<int>(bit);
            const int count = m_count[length];
            if (code - first < count)
                return m_symbols[index + code - first];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -1;
    }

private:
    std::array<uint16_t, kMaxBits + 1> m_count{};
    std::array<uint16_t, kLiteralLengthCodes> m_symbols{};
};

bool InflateCodes(BitReader& bits, const Huffman& literals, const Huffman& distances, std::vector<uint8_t>& out)
{
    for (;;)
    {
        const int symbol = literals.Decode(bits);
        if (symbol < 0)
            return false;
        if (symbol < 256)
        {
            out.push_back(static_cast<uint8_t>(symbol));
            continue;
        }
        if (symbol == 256)
            return true;
        const int lengthCode = symbol - 257;
        if (lengthCode >= 29)
            return false;
        uint32_t extra;
        if (!bits.Bits(kLengthExtra[lengthCode], extra))
            return false;
        const size_t length = kLengthBase[lengthCode] + extra;
        const int distanceCode = distances.Decode(bits);
        if (distanceCode < 0 || distanceCode >= kDistanceCodes)
            return false;
        if (!bits.Bits(kDistanceExtra[distanceCode], extra))
            return false;
        const size_t distance = kDistanceBase[distanceCode] + extra;
        if (distance > out.size())
            return false;
        // Copied byte by byte: the source may overlap the bytes being written.
        const size_t from = out.size() - distance;
        for (size_t i = 0; i < length; ++i)
            out.push_back(out[from + i]);
    }
}

bool InflateStored(BitReader& bits, std::vector<uint8_t>& out)
{
    bits.AlignToByte();
    uint32_t length, complement;
    if (!bits.Bits(16, length) || !bits.Bits(16, complement) || length != (~complement & 0xFFFF))
        return false;
    return bits.Bytes(length, out);
}

bool InflateFixed(BitReader& bits, std::vector<uint8_t>& out)
{
    static Huffman literals, distances;
    static bool built;
    if (!built)
    {
        uint8_t lengths[kLiteralLengthCodes];
        for (int i = 0; i < kLiteralLengthCodes; ++i)
            lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
        literals.Build(lengths, kLiteralLengthCodes);
        for (int i = 0; i < kDistanceCodes; ++i)
            lengths[i] = 5;
        distances.Build(lengths, kDistanceCodes);
        built = true;
    }
    return InflateCodes(bits, literals, distances, out);
}

bool InflateDynamic(BitReader& bits, std::vector<uint8_t>& out)
{
    uint32_t literalCount, distanceCount, codeLengthCount;
    if (!bits.Bits(5, literalCount) || !bits.Bits(5, distanceCount) || !bits.Bits(4, codeLengthCount))
        return false;
    literalCount += 257;
    distanceCount += 1;
    codeLengthCount += 4;
    if (literalCount > 286 || distanceCount > kDistanceCodes)
        return false;

    uint8_t lengths[kLiteralLengthCodes + kDistanceCodes] = {};
    for (uint32_t i = 0; i < codeLengthCount; ++i)
    {
        uint32_t length;
        if (!bits.Bits(3, length))
            return false;
        lengths[kCodeLengthOrder[i]] = static_cast<uint8_t>(length);
    }
    Huffman codeLengths;
    if (!codeLengths.Build(lengths, 19))
        return false;

    // The literal/length and distance code lengths, run-length encoded with the code length code.
    uint8_t all[kLiteralLengthCodes + kDistanceCodes] = {};
    uint32_t index = 0;
    while (index < literalCount + distanceCount)
    {
        const int symbol = codeLengths.Decode(bits);
        if (symbol < 0)
            return false;
        if (symbol < 16)
        {
            all[index++] = static_cast<uint8_t>(symbol);
            continue;
        }
        uint8_t value = 0;
        uint32_t repeat;
        if (symbol == 16)
        {
            if (index == 0 || !bits.Bits(2, repeat))
                return false;
            value = all[index - 1];
            repeat += 3;
        }
        else if (symbol == 17)
        {
            if (!bits.Bits(3, repeat))
                return false;
            repeat += 3;
        }
        else
        {
            if (!bits.Bits(7, repeat))
                return false;
            repeat += 11;
        }
        if (index + repeat > literalCount + distanceCount)
            return false;
        while (repeat--)
            all[index++] = value;
    }
    if (all[256] == 0)
        return false;  // no end-of-block code

    Huffman literals, distances;
    if (!literals.Build(all, static_cast<int>(literalCount)) || !distances.Build(all + literalCount, static_cast<int>(distanceCount)))
        return false;
    return InflateCodes(bits, literals, distances, out);
}
}  // namespace

bool InflateRaw(const uint8_t* in, size_t size, std::vector<uint8_t>& out)
{
    BitReader bits(in, size);
    for (;;)
    {
        uint32_t final, type;
        if (!bits.Bits(1, final) || !bits.Bits(2, type))
            return false;
        bool ok = false;
        switch (type)
        {
        case 0:
            ok = InflateStored(bits, out);
            break;
        case 1:
            ok = InflateFixed(bits, out);
            break;
        case 2:
            ok = InflateDynamic(bits, out);
            break;
        default:
            return false;
        }
        if (!ok)
            return false;
        if (final)
            return true;
    }
}
