#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <stdexcept>

namespace {

struct BitReader {
    const uint8_t* data;
    size_t size;
    size_t bytePos = 0;
    int bitPos = 0;

    BitReader(const uint8_t* d, size_t s) : data(d), size(s) {}

    int readBits(int n) {
        int result = 0;
        for (int i = 0; i < n; ++i) {
            if (bytePos >= size) return -1;
            int bit = (data[bytePos] >> bitPos) & 1;
            result |= (bit << i);
            ++bitPos;
            if (bitPos == 8) { bitPos = 0; ++bytePos; }
        }
        return result;
    }

    void align() {
        if (bitPos > 0) { bitPos = 0; ++bytePos; }
    }
};

struct HuffmanTree {
    static const int MAX_BITS = 15;
    static const int MAX_CODES = 288 + 32;

    struct Entry {
        int code;
        int len;
        int symbol;
    };
    std::vector<Entry> entries;

    bool build(const int* lengths, int n) {
        int blCount[MAX_BITS + 1] = {};
        for (int i = 0; i < n; ++i) {
            if (lengths[i] > MAX_BITS) return false;
            if (lengths[i] > 0) ++blCount[lengths[i]];
        }

        int nextCode[MAX_BITS + 1] = {};
        int code = 0;
        for (int bits = 1; bits <= MAX_BITS; ++bits) {
            code = (code + blCount[bits - 1]) << 1;
            nextCode[bits] = code;
        }

        auto reverseBits = [](int v, int bits) -> int {
            int r = 0;
            for (int i = 0; i < bits; ++i) {
                r = (r << 1) | (v & 1);
                v >>= 1;
            }
            return r;
        };

        entries.clear();
        for (int i = 0; i < n; ++i) {
            int len = lengths[i];
            if (len == 0) continue;
            int c = nextCode[len]++;
            int rev = reverseBits(c, len);
            entries.push_back({rev, len, i});
        }

        std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) { return a.len < b.len; });

        return true;
    }

    int decode(BitReader& br) const {
        int code = 0;
        for (int i = 0; i < MAX_BITS; ++i) {
            int bit = br.readBits(1);
            if (bit < 0) return -1;
            code |= (bit << i);
            for (const auto& e : entries) {
                if (e.len == i + 1 && (code & ((1 << e.len) - 1)) == e.code) {
                    return e.symbol;
                }
            }
        }
        return -1;
    }
};

const int LEN_EXTRA_BITS[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const int LEN_BASE[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const int DIST_EXTRA_BITS[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
const int DIST_BASE[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};

int fixedLitLenLengths[288];
int fixedDistLengths[32];
bool fixedInited = false;

void initFixed() {
    if (fixedInited) return;
    for (int i = 0; i <= 143; ++i) fixedLitLenLengths[i] = 8;
    for (int i = 144; i <= 255; ++i) fixedLitLenLengths[i] = 9;
    for (int i = 256; i <= 279; ++i) fixedLitLenLengths[i] = 7;
    for (int i = 280; i <= 287; ++i) fixedLitLenLengths[i] = 8;
    for (int i = 0; i < 32; ++i) fixedDistLengths[i] = 5;
    fixedInited = true;
}

// Caps total decompressed output so a small, maliciously crafted \ZIP:
// blob can't expand into an unbounded memory allocation (a "zip bomb").
// Legitimate embedded JSON/script content in a .dxx file is at most a
// few hundred KB, so this leaves generous headroom.
constexpr size_t kMaxDecompressedSize = 100 * 1024 * 1024;

std::string inflate(const uint8_t* data, size_t size) {
    std::string result;
    result.reserve(std::min(size * 3, kMaxDecompressedSize));
    BitReader br(data, size);
    bool final = false;

    HuffmanTree ftLitLen, ftDist;
    HuffmanTree dLitLen, dDist;
    initFixed();

    while (!final) {
        final = br.readBits(1) != 0;
        int type = br.readBits(2);
        if (type < 0) break;

        const HuffmanTree* litLen = nullptr;
        const HuffmanTree* dist = nullptr;

        if (type == 1) {
            ftLitLen.build(fixedLitLenLengths, 288);
            ftDist.build(fixedDistLengths, 32);
            litLen = &ftLitLen;
            dist = &ftDist;
        } else if (type == 2) {
            int hlit = br.readBits(5) + 257;
            int hdist = br.readBits(5) + 1;
            int hclen = br.readBits(4) + 4;

            int clOrder[] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            int clLengths[19] = {};
            for (int i = 0; i < hclen; ++i) {
                clLengths[clOrder[i]] = br.readBits(3);
            }
            HuffmanTree clTree;
            if (!clTree.build(clLengths, 19)) break;

            int allLengths[288 + 32] = {};
            int total = hlit + hdist;
            int idx = 0;
            while (idx < total) {
                int sym = clTree.decode(br);
                if (sym < 0) break;
                if (sym < 16) {
                    allLengths[idx++] = sym;
                } else if (sym == 16) {
                    if (idx == 0) break;
                    int repeat = br.readBits(2) + 3;
                    for (int r = 0; r < repeat && idx < total; ++r) {
                        allLengths[idx] = allLengths[idx - 1];
                        ++idx;
                    }
                } else if (sym == 17) {
                    int repeat = br.readBits(3) + 3;
                    for (int r = 0; r < repeat && idx < total; ++r)
                        allLengths[idx++] = 0;
                } else if (sym == 18) {
                    int repeat = br.readBits(7) + 11;
                    for (int r = 0; r < repeat && idx < total; ++r)
                        allLengths[idx++] = 0;
                }
            }

            dLitLen.build(allLengths, hlit);
            dDist.build(allLengths + hlit, hdist);
            litLen = &dLitLen;
            dist = &dDist;
        } else {
            br.align();
            uint16_t len = br.readBits(16);
            uint16_t nlen = br.readBits(16);
            if ((len ^ nlen) != 0xFFFF) break;
            for (int i = 0; i < len; ++i) {
                if (br.bytePos >= br.size) break;
                result += static_cast<char>(br.data[br.bytePos++]);
            }
            br.bitPos = 0;
            if (result.size() > kMaxDecompressedSize) return result;
            continue;
        }

        while (true) {
            int sym = litLen->decode(br);
            if (sym < 0) break;
            if (sym < 256) {
                result += static_cast<char>(sym);
            } else if (sym == 256) {
                break;
            } else {
                int lengthCode = sym - 257;
                if (lengthCode < 0 || lengthCode >= 29) break;
                int extraBits = LEN_EXTRA_BITS[lengthCode];
                int extra = extraBits > 0 ? br.readBits(extraBits) : 0;
                if (extra < 0) break;
                int length = LEN_BASE[lengthCode] + extra;

                int distCode = dist->decode(br);
                if (distCode < 0) break;
                if (distCode >= 30) break;
                int distExtra = DIST_EXTRA_BITS[distCode];
                int distAdd = distExtra > 0 ? br.readBits(distExtra) : 0;
                if (distAdd < 0) break;
                int distance = DIST_BASE[distCode] + distAdd;

                size_t src = result.size() - distance;
                if (distance > result.size()) break;
                for (int i = 0; i < length; ++i) {
                    result += result[src + i];
                }
            }
            if (result.size() > kMaxDecompressedSize) return result;
        }
    }

    return result;
}

std::string gzipDecompress(const std::string& data) {
    if (data.size() < 18) return "";
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data.data());
    if (d[0] != 0x1F || d[1] != 0x8B) return "";
    if (d[2] != 8) return "";

    size_t offset = 10;
    if (offset > data.size()) return "";
    uint8_t flags = d[3];
    if (flags & 0x04) {
        if (offset + 2 > data.size()) return "";
        uint16_t xlen = d[offset] | (d[offset + 1] << 8);
        offset += 2 + xlen;
        if (offset > data.size()) return "";
    }
    if (flags & 0x08) {
        while (offset < data.size() && d[offset] != 0) ++offset;
        if (offset >= data.size()) return "";
        ++offset;
    }
    if (flags & 0x10) {
        while (offset < data.size() && d[offset] != 0) ++offset;
        if (offset >= data.size()) return "";
        ++offset;
    }
    if (flags & 0x02) {
        if (offset + 2 > data.size()) return "";
        offset += 2;
    }

    if (offset + 8 > data.size()) return "";
    size_t deflateLen = data.size() - offset - 8;
    return inflate(d + offset, deflateLen);
}

} // anonymous namespace

namespace dxx {
std::string GzipDecompress(const std::string& compressed) {
    return gzipDecompress(compressed);
}
} // namespace dxx
