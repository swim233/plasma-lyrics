#pragma once

#include <QByteArray>
#include <QString>
#include <array>
#include <optional>

namespace PlasmaLyrics {

/// QQ Music's QRC payload cipher: a deliberately non-standard triple DES
/// followed by a zlib stream.  It lives under providers/ because it serves
/// exactly one integration and has no use anywhere else -- and because it is
/// emphatically NOT a security primitive.  The S-boxes, the PC-2 table and
/// the initial permutations were all altered by whoever wrote the original
/// client, so a stock DES implementation decrypts every block without
/// complaint and produces rubbish.  That rubbish never reaches a caller.
/// What the cipher feeds is a zlib stream, so there are only two outcomes --
/// it inflates and the plaintext is bit-exact, or it does not inflate at all
/// -- and a wrong table lands in the second: across 860 mutation trials none
/// ever inflated to anything.  The binding check is zlib's two-byte header
/// and then Huffman validity, which rejects before a single byte of output
/// exists; the adler32 at the end has never had to fire.  See DESIGN.md
/// decision 71 for the enumerated deviations, the verification evidence and
/// the four limits on what it does and does not establish; decision 68 is
/// the provider that uses this.
namespace QrcCipher {

/// One DES key schedule: sixteen 48-bit round keys, each held as six bytes.
using KeySchedule = std::array<std::array<quint8, 6>, 16>;

/// The three fixed keys, applied in the order key3/key2/key1.  They are
/// published constants of the format, not secrets.
QByteArray key1();
QByteArray key2();
QByteArray key3();

/// Exposed so the deviations can be tested where they are introduced rather
/// than only through an end-to-end decrypt: the schedule isolates the
/// little-endian key word read and the PC-2 D-half offset, while a single
/// block additionally exercises the two altered S-box entries and the
/// non-standard IP / inverse IP.
KeySchedule keySchedule(QByteArrayView key, bool decrypt);
std::array<quint8, 8> cryptBlock(const std::array<quint8, 8> &block, const KeySchedule &schedule);

/// Runs the three schedules over one block in the order the format uses.
std::array<quint8, 8> tripleDesBlock(const std::array<quint8, 8> &block);

/// Decrypts a hex-encoded QRC payload into its UTF-8 text.  Returns nullopt
/// and sets *error on malformed hex, a truncated body or a zlib stream that
/// does not inflate.  The UTF-8 BOM the payload carries is stripped.
std::optional<QString> decryptHex(QByteArrayView hex, QString *error = nullptr);

/// The raw-bytes entry point behind decryptHex, split out so a caller that
/// already holds the ciphertext does not have to re-encode it.
std::optional<QString> decrypt(QByteArrayView cipherText, QString *error = nullptr);

} // namespace QrcCipher

} // namespace PlasmaLyrics
