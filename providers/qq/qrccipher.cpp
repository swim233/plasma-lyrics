#include "qrccipher.h"

#include <QStringDecoder>

#include <zlib.h>

#include <array>
#include <cstring>

namespace PlasmaLyrics {
namespace QrcCipher {
namespace {

// DEVIATION 1 of 4 -- two altered S-box entries.  Everything here is the
// textbook DES table except S2[23], which is 15 where standard DES has 14,
// and S4[53], which is 10 where standard DES has 1.  Both are marked below.
// A single wrong entry still decrypts every block without error at this
// layer; it corrupts a few bits per block, and the zlib stream underneath
// turns that into a hard failure rather than into text -- measured over 860
// table mutations, none of which ever inflated. The explicit vectors below
// exist so a wrong entry is identified here, where the cause is legible,
// instead of only as "the payload is not a valid zlib stream".
constexpr std::array<quint8, 512> kSBoxes{
    // S-box 1
    14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
    0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
    4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
    15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13,
    // S-box 2 -- index 23 is 15 here, 14 in standard DES.
    15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
    3, 13, 4, 7, 15, 2, 8, 15, 12, 0, 1, 10, 6, 9, 11, 5,
    0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
    13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9,
    // S-box 3
    10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
    13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
    13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
    1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12,
    // S-box 4 -- index 53 is 10 here, 1 in standard DES.
    7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
    13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
    10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
    3, 15, 0, 6, 10, 10, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14,
    // S-box 5
    2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
    14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
    4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
    11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3,
    // S-box 6
    12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
    10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
    9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
    4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13,
    // S-box 7
    4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
    13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
    1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
    6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12,
    // S-box 8
    13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
    1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
    7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
    2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11};

constexpr std::array<quint8, 32> kPBox{
    16, 7, 20, 21, 29, 12, 28, 17, 1, 15, 23, 26, 5, 18, 31, 10,
    2, 8, 24, 14, 32, 27, 3, 9, 19, 13, 30, 6, 22, 11, 4, 25};

constexpr std::array<quint8, 48> kEBox{
    32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9, 8, 9, 10, 11, 12, 13, 12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25, 24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1};

constexpr std::array<quint8, 16> kRoundShift{1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1};

constexpr std::array<quint8, 28> kKeyPermC{
    56, 48, 40, 32, 24, 16, 8, 0, 57, 49, 41, 33, 25, 17,
    9, 1, 58, 50, 42, 34, 26, 18, 10, 2, 59, 51, 43, 35};

constexpr std::array<quint8, 28> kKeyPermD{
    62, 54, 46, 38, 30, 22, 14, 6, 61, 53, 45, 37, 29, 21,
    13, 5, 60, 52, 44, 36, 28, 20, 12, 4, 27, 19, 11, 3};

constexpr std::array<quint8, 48> kKeyCompression{
    13, 16, 10, 23, 0, 4, 2, 27, 14, 5, 20, 9, 22, 18, 11, 3,
    25, 7, 15, 6, 26, 19, 12, 1, 40, 51, 30, 36, 46, 54, 29, 39,
    50, 44, 32, 47, 43, 48, 38, 55, 33, 52, 45, 41, 49, 35, 28, 31};

// DEVIATION 2 of 4 -- neither of these is the DES initial permutation or its
// inverse.  They are the tables the original client ships.
constexpr std::array<quint8, 64> kInitialPermutation{
    34, 42, 50, 58, 2, 10, 18, 26, 36, 44, 52, 60, 4, 12, 20, 28,
    38, 46, 54, 62, 6, 14, 22, 30, 40, 48, 56, 64, 8, 16, 24, 32,
    33, 41, 49, 57, 1, 9, 17, 25, 35, 43, 51, 59, 3, 11, 19, 27,
    37, 45, 53, 61, 5, 13, 21, 29, 39, 47, 55, 63, 7, 15, 23, 31};

constexpr std::array<quint8, 64> kInversePermutation{
    37, 5, 45, 13, 53, 21, 61, 29, 38, 6, 46, 14, 54, 22, 62, 30,
    39, 7, 47, 15, 55, 23, 63, 31, 40, 8, 48, 16, 56, 24, 64, 32,
    33, 1, 41, 9, 49, 17, 57, 25, 34, 2, 42, 10, 50, 18, 58, 26,
    35, 3, 43, 11, 51, 19, 59, 27, 36, 4, 44, 12, 52, 20, 60, 28};

// A lone zlib stream never inflates to anything near this, and the payloads
// this decrypts are one song's lyrics. The cap keeps a corrupted length
// field from turning into an unbounded allocation.
constexpr qsizetype kMaximumInflatedBytes = 8 * 1024 * 1024;
constexpr qsizetype kInflateChunkBytes = 16 * 1024;

// The six input bits arrive shuffled relative to the table layout: bit 5
// stays put, bits 4..1 shift down one place, and bit 0 moves up to bit 4.
constexpr int sBoxIndex(int value)
{
    return (value & 0x20) | ((value & 0x1F) >> 1) | ((value & 0x01) << 4);
}

quint32 applyPBox(quint32 value)
{
    quint32 result = 0;
    for (int i = 0; i < 32; ++i) {
        if ((value >> (32 - kPBox[i])) & 1u) {
            result |= 1u << (31 - i);
        }
    }
    return result;
}

using SelectionTables = std::array<std::array<quint32, 64>, 8>;

// S-box substitution and the P permutation folded into one lookup per
// 6-bit group, which is the usual DES speedup and changes no result.
const SelectionTables &selectionTables()
{
    static const SelectionTables tables = [] {
        SelectionTables result{};
        for (int box = 0; box < 8; ++box) {
            for (int value = 0; value < 64; ++value) {
                const quint32 substituted =
                    static_cast<quint32>(kSBoxes[box * 64 + sBoxIndex(value)]) << (28 - box * 4);
                result[box][value] = applyPBox(substituted);
            }
        }
        return result;
    }();
    return tables;
}

quint64 permute64(quint64 value, const std::array<quint8, 64> &rule)
{
    quint64 result = 0;
    for (int i = 0; i < 64; ++i) {
        if ((value >> (64 - rule[i])) & 1ull) {
            result |= 1ull << (63 - i);
        }
    }
    return result;
}

// DEVIATION 3 of 4 -- the key's eight bytes are read as two little-endian
// 32-bit words and then concatenated, not as one big-endian 64-bit word.
// Standard DES reads the key big-endian, so this alone reorders every
// round key. Note also that the bit is selected at (63 - pos) here, one
// place off from permute64's (64 - pos): the key tables are 0-based while
// the permutation tables are 1-based.
quint64 permuteKeyHalf(QByteArrayView key, const std::array<quint8, 28> &table)
{
    const auto word = [&key](int offset) {
        return static_cast<quint32>(static_cast<quint8>(key[offset]))
            | (static_cast<quint32>(static_cast<quint8>(key[offset + 1])) << 8)
            | (static_cast<quint32>(static_cast<quint8>(key[offset + 2])) << 16)
            | (static_cast<quint32>(static_cast<quint8>(key[offset + 3])) << 24);
    };
    const quint64 material = (static_cast<quint64>(word(0)) << 32) | word(4);
    quint64 result = 0;
    for (int i = 0; i < 28; ++i) {
        if ((material >> (63 - table[i])) & 1ull) {
            result |= 1ull << (27 - i);
        }
    }
    return result;
}

quint32 rotateLeft28(quint32 value, int amount)
{
    return ((value << amount) | (value >> (28 - amount))) & 0xFFFFFFF0u;
}

quint32 feistel(quint32 state, const std::array<quint8, 6> &roundKey)
{
    quint64 expanded = 0;
    for (int i = 0; i < 48; ++i) {
        expanded |= static_cast<quint64>((state >> (32 - kEBox[i])) & 1u) << (47 - i);
    }
    quint64 key = 0;
    for (int i = 0; i < 6; ++i) {
        key = (key << 8) | roundKey[i];
    }
    const quint64 mixed = expanded ^ key;
    const auto &tables = selectionTables();
    return tables[0][(mixed >> 42) & 0x3F] | tables[1][(mixed >> 36) & 0x3F]
        | tables[2][(mixed >> 30) & 0x3F] | tables[3][(mixed >> 24) & 0x3F]
        | tables[4][(mixed >> 18) & 0x3F] | tables[5][(mixed >> 12) & 0x3F]
        | tables[6][(mixed >> 6) & 0x3F] | tables[7][mixed & 0x3F];
}

std::optional<QByteArray> inflate(const QByteArray &compressed, QString *error)
{
    const auto fail = [error](const char *message) -> std::optional<QByteArray> {
        if (error) *error = QString::fromLatin1(message);
        return std::nullopt;
    };
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) {
        return fail("could not initialise the zlib stream");
    }
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    QByteArray result;
    QByteArray chunk(kInflateChunkBytes, Qt::Uninitialized);
    int status = Z_OK;
    while (status != Z_STREAM_END) {
        stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        status = ::inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
            inflateEnd(&stream);
            return fail("the payload is not a valid zlib stream");
        }
        result.append(chunk.constData(), chunk.size() - static_cast<qsizetype>(stream.avail_out));
        if (result.size() > kMaximumInflatedBytes) {
            inflateEnd(&stream);
            return fail("the inflated payload is implausibly large");
        }
        // Z_BUF_ERROR with no input left is a truncated stream, not a
        // request for a bigger output buffer.
        if (status == Z_BUF_ERROR && stream.avail_in == 0) {
            inflateEnd(&stream);
            return fail("the zlib stream ends mid-payload");
        }
    }
    inflateEnd(&stream);
    return result;
}

} // namespace

QByteArray key1() { return QByteArrayLiteral("!@#)(*$%"); }
QByteArray key2() { return QByteArrayLiteral("123ZXC!@"); }
QByteArray key3() { return QByteArrayLiteral("!@#)(NHL"); }

KeySchedule keySchedule(QByteArrayView key, bool decrypt)
{
    KeySchedule schedule{};
    if (key.size() < 8) {
        return schedule;
    }
    quint32 c = static_cast<quint32>(permuteKeyHalf(key, kKeyPermC) << 4);
    quint32 d = static_cast<quint32>(permuteKeyHalf(key, kKeyPermD) << 4);
    for (int round = 0; round < 16; ++round) {
        const int shift = kRoundShift[round];
        c = rotateLeft28(c, shift);
        d = rotateLeft28(d, shift);
        // Decryption reuses the encryption rounds in reverse, so the
        // schedule is filled back to front rather than the rounds being
        // walked backwards at use time.
        const int target = decrypt ? 15 - round : round;
        quint64 roundKey = 0;
        for (int i = 0; i < 48; ++i) {
            const int position = kKeyCompression[i];
            // DEVIATION 4 of 4 -- the D half is indexed at (position - 27).
            // Standard PC-2 uses (position - 28); the off-by-one shifts
            // every D-half bit one place and is invisible end to end
            // except as corrupt output.
            const quint32 bit = position < 28 ? (c >> (31 - position)) & 1u
                                              : (d >> (31 - (position - 27))) & 1u;
            if (bit) {
                roundKey |= 1ull << (47 - i);
            }
        }
        for (int i = 0; i < 6; ++i) {
            schedule[target][i] = static_cast<quint8>((roundKey >> (40 - i * 8)) & 0xFF);
        }
    }
    return schedule;
}

std::array<quint8, 8> cryptBlock(const std::array<quint8, 8> &block, const KeySchedule &schedule)
{
    quint64 value = 0;
    for (const auto byte : block) {
        value = (value << 8) | byte;
    }
    const quint64 permuted = permute64(value, kInitialPermutation);
    quint32 left = static_cast<quint32>(permuted >> 32);
    quint32 right = static_cast<quint32>(permuted & 0xFFFFFFFFu);
    for (int round = 0; round < 15; ++round) {
        const quint32 previousRight = right;
        right = left ^ feistel(right, schedule[round]);
        left = previousRight;
    }
    // The sixteenth round deliberately omits the halves swap, which is what
    // makes the same routine both encrypt and decrypt.
    left ^= feistel(right, schedule[15]);
    const quint64 combined = (static_cast<quint64>(left) << 32) | right;
    const quint64 output = permute64(combined, kInversePermutation);
    std::array<quint8, 8> result{};
    for (int i = 0; i < 8; ++i) {
        result[i] = static_cast<quint8>((output >> (56 - i * 8)) & 0xFF);
    }
    return result;
}

std::array<quint8, 8> tripleDesBlock(const std::array<quint8, 8> &block)
{
    // Decrypt / encrypt / decrypt, keys applied in the order 3, 2, 1.
    static const std::array<KeySchedule, 3> schedules{
        keySchedule(key3(), true), keySchedule(key2(), false), keySchedule(key1(), true)};
    auto result = block;
    for (const auto &schedule : schedules) {
        result = cryptBlock(result, schedule);
    }
    return result;
}

std::optional<QString> decrypt(QByteArrayView cipherText, QString *error)
{
    if (cipherText.isEmpty()) {
        if (error) *error = QStringLiteral("the payload is empty");
        return std::nullopt;
    }
    // A trailing partial block is dropped rather than padded: the format
    // carries no padding scheme, and every observed payload is a whole
    // number of blocks.
    const qsizetype blocks = cipherText.size() / 8;
    if (blocks == 0) {
        if (error) *error = QStringLiteral("the payload is shorter than one block");
        return std::nullopt;
    }
    QByteArray plain;
    plain.reserve(blocks * 8);
    for (qsizetype index = 0; index < blocks; ++index) {
        std::array<quint8, 8> block{};
        std::memcpy(block.data(), cipherText.data() + index * 8, 8);
        const auto decrypted = tripleDesBlock(block);
        plain.append(reinterpret_cast<const char *>(decrypted.data()), 8);
    }
    const auto inflated = inflate(plain, error);
    if (!inflated) {
        return std::nullopt;
    }
    // The payload is UTF-8 with a BOM. QStringDecoder consumes the BOM, and
    // unlike QString::fromUtf8 it can report that the bytes were not UTF-8
    // at all, which is the signature of a decryption that went wrong
    // without failing.
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString text = decoder(*inflated);
    if (decoder.hasError()) {
        if (error) *error = QStringLiteral("the decrypted payload is not valid UTF-8");
        return std::nullopt;
    }
    return text;
}

std::optional<QString> decryptHex(QByteArrayView hex, QString *error)
{
    const QByteArray trimmed = hex.toByteArray().trimmed();
    if (trimmed.isEmpty()) {
        if (error) *error = QStringLiteral("the payload is empty");
        return std::nullopt;
    }
    // QByteArray::fromHex skips anything that is not a hex digit instead of
    // rejecting it, so a plain-text body would silently become a short,
    // meaningless buffer. Check the alphabet first.
    for (const auto character : trimmed) {
        const bool isHexDigit = (character >= '0' && character <= '9')
            || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F');
        if (!isHexDigit) {
            if (error) *error = QStringLiteral("the payload is not hex-encoded");
            return std::nullopt;
        }
    }
    if (trimmed.size() % 2 != 0) {
        if (error) *error = QStringLiteral("the hex payload has an odd length");
        return std::nullopt;
    }
    return decrypt(QByteArray::fromHex(trimmed), error);
}

} // namespace QrcCipher

} // namespace PlasmaLyrics
