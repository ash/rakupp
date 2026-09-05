// Zlib.cpp — see Zlib.h for the contract and for why this is not a port of the
// distribution's C.
//
// Inflate is written in the shape Mark Adler's `puff` uses for its Huffman
// decoding — a canonical-code table of counts and symbols, walked one bit at a
// time — because that shape is the one whose correctness can be read off the
// specification, and inflate's job here is to be right about untrusted input
// rather than to be quick. Deflate is LZ77 over a hash chain, then a dynamic
// Huffman block, with a stored block and a fixed-Huffman block both costed and
// the smallest of the three emitted.

#include "Zlib.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace rakupp {

namespace {

[[noreturn]] void zdie(const std::string& m) { throw ZlibError(m); }

// ---- the two checksums ----------------------------------------------------

const uint32_t* crcTable() {
    static uint32_t t[256];
    static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        built = true;
    }
    return t;
}

// ---- inflate --------------------------------------------------------------

struct BitIn {
    const uint8_t* p;
    const uint8_t* end;
    uint32_t buf = 0;
    int cnt = 0;

    uint32_t bits(int need) {
        uint32_t v = buf;
        while (cnt < need) {
            if (p >= end) zdie("inflate: stream ends in the middle of a code");
            v |= (uint32_t)(*p++) << cnt;
            cnt += 8;
        }
        buf = v >> need;
        cnt -= need;
        return v & ((1u << need) - 1);
    }
    void align() { buf = 0; cnt = 0; }
};

// A canonical Huffman code, as counts per length plus the symbols in order.
struct Huff {
    short count[16];
    short symbol[288];
};

void buildHuff(Huff& h, const unsigned char* len, int n, const char* what) {
    for (int i = 0; i < 16; i++) h.count[i] = 0;
    for (int i = 0; i < n; i++) h.count[len[i]]++;
    if (h.count[0] == n) zdie(std::string("inflate: ") + what + " has no codes at all");

    // Over-subscribed is a corrupt stream; INCOMPLETE is legal for the distance
    // code alone (a block with no matches in it), so the caller decides.
    int left = 1;
    for (int l = 1; l < 16; l++) {
        left <<= 1;
        left -= h.count[l];
        if (left < 0) zdie(std::string("inflate: ") + what + " is over-subscribed");
    }
    short offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = offs[l] + h.count[l];
    for (int i = 0; i < n; i++) if (len[i]) h.symbol[offs[len[i]]++] = (short)i;
}

int decodeSym(BitIn& b, const Huff& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= (int)b.bits(1);
        int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    zdie("inflate: no Huffman code matches the next 15 bits");
}

const unsigned short kLenBase[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const unsigned short kLenExtra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const unsigned short kDistBase[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const unsigned short kDistExtra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
const unsigned char kClOrder[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

void fixedTables(Huff& lit, Huff& dist) {
    unsigned char l[288];
    for (int i = 0;   i < 144; i++) l[i] = 8;
    for (int i = 144; i < 256; i++) l[i] = 9;
    for (int i = 256; i < 280; i++) l[i] = 7;
    for (int i = 280; i < 288; i++) l[i] = 8;
    buildHuff(lit, l, 288, "the fixed literal code");
    unsigned char d[30];
    for (int i = 0; i < 30; i++) d[i] = 5;
    buildHuff(dist, d, 30, "the fixed distance code");
}

void inflateCodes(BitIn& b, std::string& out, const Huff& lit, const Huff& dist) {
    for (;;) {
        int sym = decodeSym(b, lit);
        if (sym < 256) { out += (char)(unsigned char)sym; continue; }
        if (sym == 256) return;
        sym -= 257;
        if (sym >= 29) zdie("inflate: length symbol 285-287 is reserved and cannot appear");
        size_t len = kLenBase[sym] + b.bits(kLenExtra[sym]);
        int dsym = decodeSym(b, dist);
        if (dsym >= 30) zdie("inflate: distance symbol 30 or 31 is reserved");
        size_t d = kDistBase[dsym] + b.bits(kDistExtra[dsym]);
        if (d > out.size()) zdie("inflate: a match points back before the start of the output");
        // Byte at a time, deliberately: a run may overlap itself (distance 1 is
        // how a repeated byte is coded), so memcpy would be wrong here.
        size_t from = out.size() - d;
        for (size_t i = 0; i < len; i++) out += out[from + i];
    }
}

void inflateRaw(BitIn& b, std::string& out) {
    for (;;) {
        uint32_t last = b.bits(1);
        uint32_t type = b.bits(2);
        if (type == 0) {
            b.align();
            if (b.end - b.p < 4) zdie("inflate: stored block header is truncated");
            unsigned len  = b.p[0] | (unsigned)b.p[1] << 8;
            unsigned nlen = b.p[2] | (unsigned)b.p[3] << 8;
            b.p += 4;
            if ((len ^ 0xFFFFu) != nlen) zdie("inflate: stored block length does not match its complement");
            if ((size_t)(b.end - b.p) < len) zdie("inflate: stored block runs past the end of the stream");
            out.append((const char*)b.p, len);
            b.p += len;
        }
        else if (type == 1) {
            Huff lit, dist;
            fixedTables(lit, dist);
            inflateCodes(b, out, lit, dist);
        }
        else if (type == 2) {
            int nlen  = (int)b.bits(5) + 257;
            int ndist = (int)b.bits(5) + 1;
            int ncode = (int)b.bits(4) + 4;
            if (nlen > 286 || ndist > 30) zdie("inflate: dynamic block declares too many codes");
            unsigned char lengths[288 + 30];
            std::memset(lengths, 0, sizeof lengths);
            unsigned char cl[19];
            std::memset(cl, 0, sizeof cl);
            for (int i = 0; i < ncode; i++) cl[kClOrder[i]] = (unsigned char)b.bits(3);
            Huff clh;
            buildHuff(clh, cl, 19, "the code-length code");
            for (int i = 0; i < nlen + ndist; ) {
                int sym = decodeSym(b, clh);
                if (sym < 16) lengths[i++] = (unsigned char)sym;
                else {
                    int rep, val = 0;
                    if (sym == 16) {
                        if (i == 0) zdie("inflate: a repeat code appears before any length to repeat");
                        val = lengths[i - 1];
                        rep = 3 + (int)b.bits(2);
                    }
                    else if (sym == 17) rep = 3  + (int)b.bits(3);
                    else                rep = 11 + (int)b.bits(7);
                    if (i + rep > nlen + ndist) zdie("inflate: a repeat runs past the end of the code lengths");
                    while (rep--) lengths[i++] = (unsigned char)val;
                }
            }
            if (lengths[256] == 0) zdie("inflate: the block has no end-of-block code");
            Huff lit, dist;
            buildHuff(lit, lengths, nlen, "the literal/length code");
            // An INCOMPLETE distance code is legal when the block holds no
            // matches; buildHuff refuses only the all-zero and over-subscribed
            // shapes, which are the ones that are always wrong.
            buildHuff(dist, lengths + nlen, ndist, "the distance code");
            inflateCodes(b, out, lit, dist);
        }
        else zdie("inflate: block type 3 is reserved");
        if (last) return;
    }
}

// ---- deflate --------------------------------------------------------------

struct BitOut {
    std::string out;
    uint32_t buf = 0;
    int cnt = 0;
    void put(uint32_t v, int n) {              // LSB first, as the format is
        buf |= (v & ((1u << n) - 1)) << cnt;
        cnt += n;
        while (cnt >= 8) { out += (char)(buf & 0xFF); buf >>= 8; cnt -= 8; }
    }
    void putRev(uint32_t code, int n) {        // a Huffman code is MSB first
        for (int i = n - 1; i >= 0; i--) put((code >> i) & 1, 1);
    }
    void align() { if (cnt) { out += (char)(buf & 0xFF); buf = 0; cnt = 0; } }
};

// Code lengths for one alphabet, length-limited to `maxLen`. The frequencies
// are turned into a tree by repeatedly merging the two rarest nodes; the limit
// is then imposed by the standard shift, which moves a leaf up from the deepest
// level and pays for it by pushing a shallower one down. Both are what zlib
// does, and the result only has to be A valid prefix code, not the optimal one.
void codeLengths(const std::vector<uint32_t>& freq, int maxLen, std::vector<unsigned char>& len) {
    const int n = (int)freq.size();
    len.assign(n, 0);
    struct Node { uint32_t f; int left, right, sym; };
    std::vector<Node> nodes;
    std::vector<int> heap;
    for (int i = 0; i < n; i++)
        if (freq[i]) { nodes.push_back({freq[i], -1, -1, i}); heap.push_back((int)nodes.size() - 1); }

    if (heap.empty()) return;
    if (heap.size() == 1) { len[nodes[heap[0]].sym] = 1; return; }

    auto cmp = [&](int a, int b) { return nodes[a].f > nodes[b].f; };  // min-heap
    std::make_heap(heap.begin(), heap.end(), cmp);
    while (heap.size() > 1) {
        std::pop_heap(heap.begin(), heap.end(), cmp); int a = heap.back(); heap.pop_back();
        std::pop_heap(heap.begin(), heap.end(), cmp); int b = heap.back(); heap.pop_back();
        nodes.push_back({nodes[a].f + nodes[b].f, a, b, -1});
        heap.push_back((int)nodes.size() - 1);
        std::push_heap(heap.begin(), heap.end(), cmp);
    }
    // Depths, iteratively — a pathological tree is 286 deep and the stack is
    // not the place to find that out.
    std::vector<std::pair<int,int>> stk{{heap[0], 0}};
    std::vector<int> depth(nodes.size(), 0);
    while (!stk.empty()) {
        auto [i, d] = stk.back(); stk.pop_back();
        if (nodes[i].sym >= 0) { depth[i] = d ? d : 1; len[nodes[i].sym] = (unsigned char)(d ? d : 1); continue; }
        stk.push_back({nodes[i].left, d + 1});
        stk.push_back({nodes[i].right, d + 1});
    }

    // Impose the limit. Count how many codes sit at each length, then move the
    // overflow up one level at a time, each move paid for by demoting the
    // shallowest available leaf.
    std::vector<int> blCount(maxLen + 2, 0);
    int over = 0;
    for (int i = 0; i < n; i++) {
        if (!len[i]) continue;
        if (len[i] > maxLen) { over += 1 << (maxLen - 0); len[i] = (unsigned char)maxLen; }
    }
    for (int i = 0; i < n; i++) if (len[i]) blCount[len[i]]++;
    // Kraft sum over 2^-len, in units of 2^-maxLen.
    long long kraft = 0;
    for (int l = 1; l <= maxLen; l++) kraft += (long long)blCount[l] << (maxLen - l);
    const long long full = 1LL << maxLen;
    while (kraft > full) {
        // Demote a leaf: find the deepest length below maxLen holding one, push
        // it down one level. Each demotion halves that leaf's share.
        int l = maxLen - 1;
        while (l >= 1 && blCount[l] == 0) l--;
        if (l < 1) break;
        blCount[l]--;
        blCount[l + 1] += 2;
        blCount[maxLen]--;
        kraft -= 1LL << (maxLen - l - 1);
    }
    if (kraft > full) zdie("deflate: cannot fit the code within its length limit");
    // Rewrite the lengths from blCount, longest symbols first, so the counts
    // and the per-symbol lengths agree.
    std::vector<int> order;
    for (int i = 0; i < n; i++) if (len[i]) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        if (len[a] != len[b]) return len[a] > len[b];
        return a < b;
    });
    size_t k = 0;
    for (int l = maxLen; l >= 1; l--)
        for (int c = 0; c < blCount[l]; c++)
            if (k < order.size()) len[order[k++]] = (unsigned char)l;
}

// Canonical codes from lengths, exactly as RFC 1951 section 3.2.2 spells it.
void canonical(const std::vector<unsigned char>& len, std::vector<uint32_t>& code) {
    code.assign(len.size(), 0);
    int maxLen = 0;
    for (auto l : len) maxLen = std::max(maxLen, (int)l);
    std::vector<uint32_t> blCount(maxLen + 1, 0), next(maxLen + 1, 0);
    for (auto l : len) if (l) blCount[l]++;
    uint32_t c = 0;
    for (int l = 1; l <= maxLen; l++) { c = (c + blCount[l - 1]) << 1; next[l] = c; }
    for (size_t i = 0; i < len.size(); i++) if (len[i]) code[i] = next[len[i]]++;
}

int lenSym(size_t len) {
    for (int i = 28; i >= 0; i--) if (len >= kLenBase[i]) return i;
    return 0;
}
int distSym(size_t d) {
    for (int i = 29; i >= 0; i--) if (d >= kDistBase[i]) return i;
    return 0;
}

struct Token { uint16_t lit; uint16_t len; uint16_t dist; };  // dist 0 = a literal

// LZ77 over a hash chain. `good` is how far down a chain to look, which is the
// whole of what the level means here.
void lz77(const std::string& in, int level, std::vector<Token>& out) {
    const size_t n = in.size();
    const int maxChain = level <= 0 ? 0 : level <= 3 ? 8 : level <= 6 ? 64 : 512;
    const size_t MINM = 3, MAXM = 258, WINDOW = 32768;
    std::vector<int> head(1 << 15, -1), prev(n + 1, -1);
    auto h3 = [&](size_t i) -> uint32_t {
        return (uint32_t)(((unsigned char)in[i] << 10) ^ ((unsigned char)in[i+1] << 5)
                          ^ (unsigned char)in[i+2]) & 0x7FFF;
    };
    size_t i = 0;
    while (i < n) {
        size_t bestLen = 0, bestDist = 0;
        if (maxChain && i + MINM <= n) {
            uint32_t hv = h3(i);
            int cand = head[hv];
            int chain = maxChain;
            while (cand >= 0 && chain--) {
                size_t d = i - (size_t)cand;
                if (d == 0 || d > WINDOW) break;
                size_t maxHere = std::min(MAXM, n - i);
                if (maxHere <= bestLen) break;
                size_t l = 0;
                while (l < maxHere && in[cand + l] == in[i + l]) l++;
                if (l > bestLen) { bestLen = l; bestDist = d; if (l == MAXM) break; }
                cand = prev[cand];
            }
        }
        if (bestLen >= MINM) {
            out.push_back({0, (uint16_t)bestLen, (uint16_t)bestDist});
            for (size_t k = 0; k < bestLen; k++) {
                if (i + k + MINM <= n) {
                    uint32_t hv = h3(i + k);
                    prev[i + k] = head[hv];
                    head[hv] = (int)(i + k);
                }
            }
            i += bestLen;
        }
        else {
            out.push_back({(uint16_t)(unsigned char)in[i], 0, 0});
            if (i + MINM <= n) { uint32_t hv = h3(i); prev[i] = head[hv]; head[hv] = (int)i; }
            i++;
        }
    }
}

// The cost in bits of coding `tokens` with the given code lengths, plus the
// extra bits the length and distance symbols carry.
long long tokenCost(const std::vector<Token>& toks,
                    const std::vector<unsigned char>& litLen,
                    const std::vector<unsigned char>& dstLen) {
    long long bits = 0;
    for (auto& t : toks) {
        if (!t.dist) { bits += litLen[t.lit]; continue; }
        int ls = lenSym(t.len), ds = distSym(t.dist);
        bits += litLen[257 + ls] + kLenExtra[ls] + dstLen[ds] + kDistExtra[ds];
    }
    return bits + litLen[256];
}

void emitTokens(BitOut& b, const std::vector<Token>& toks,
                const std::vector<unsigned char>& litLen, const std::vector<uint32_t>& litCode,
                const std::vector<unsigned char>& dstLen, const std::vector<uint32_t>& dstCode) {
    for (auto& t : toks) {
        if (!t.dist) { b.putRev(litCode[t.lit], litLen[t.lit]); continue; }
        int ls = lenSym(t.len), ds = distSym(t.dist);
        b.putRev(litCode[257 + ls], litLen[257 + ls]);
        b.put(t.len - kLenBase[ls], kLenExtra[ls]);
        b.putRev(dstCode[ds], dstLen[ds]);
        b.put(t.dist - kDistBase[ds], kDistExtra[ds]);
    }
    b.putRev(litCode[256], litLen[256]);
}

// The code lengths themselves are Huffman-coded, over an alphabet of 0-15 plus
// three repeat forms. Returns the run-length symbols and their extra bits.
void clEncode(const std::vector<unsigned char>& lens,
              std::vector<std::pair<int,int>>& out) {   // (symbol, extra value)
    size_t i = 0;
    while (i < lens.size()) {
        int v = lens[i];
        size_t run = 1;
        while (i + run < lens.size() && lens[i + run] == v) run++;
        if (v == 0) {
            while (run >= 11) { size_t k = std::min<size_t>(run, 138); out.push_back({18, (int)k - 11}); run -= k; i += k; }
            while (run >= 3)  { size_t k = std::min<size_t>(run, 10);  out.push_back({17, (int)k - 3});  run -= k; i += k; }
            while (run--) { out.push_back({0, 0}); i++; }
        }
        else {
            out.push_back({v, 0}); i++; run--;
            while (run >= 3) { size_t k = std::min<size_t>(run, 6); out.push_back({16, (int)k - 3}); run -= k; i += k; }
            while (run--) { out.push_back({v, 0}); i++; }
        }
    }
}

void deflateBlocks(const std::string& in, int level, std::string& out) {
    if (level < 0) level = 6;

    // A stored block is the floor, and for level 0 or incompressible input it
    // is also the answer. 65535 bytes is the largest one the format allows.
    auto emitStored = [&](BitOut& b) {
        size_t off = 0;
        do {
            size_t n = std::min<size_t>(65535, in.size() - off);
            b.put(off + n >= in.size() ? 1 : 0, 1);
            b.put(0, 2);
            b.align();
            b.out += (char)(n & 0xFF); b.out += (char)((n >> 8) & 0xFF);
            b.out += (char)(~n & 0xFF); b.out += (char)((~n >> 8) & 0xFF);
            b.out.append(in, off, n);
            off += n;
        } while (off < in.size());
    };

    if (level == 0 || in.empty()) {
        BitOut b;
        if (in.empty()) { b.put(1, 1); b.put(0, 2); b.align(); b.out.append(4, 0); b.out[b.out.size()-2] = (char)0xFF; b.out[b.out.size()-1] = (char)0xFF; }
        else emitStored(b);
        out += b.out;
        return;
    }

    std::vector<Token> toks;
    lz77(in, level, toks);

    std::vector<uint32_t> litFreq(288, 0), dstFreq(30, 0);
    for (auto& t : toks) {
        if (!t.dist) litFreq[t.lit]++;
        else { litFreq[257 + lenSym(t.len)]++; dstFreq[distSym(t.dist)]++; }
    }
    litFreq[256]++;                                    // the end-of-block code

    std::vector<unsigned char> litLen, dstLen;
    codeLengths(litFreq, 15, litLen);
    codeLengths(dstFreq, 15, dstLen);
    // At least one distance code must exist, even in a block with no matches:
    // an empty distance alphabet has no valid encoding in the header.
    if (std::all_of(dstLen.begin(), dstLen.end(), [](unsigned char c) { return c == 0; }))
        dstLen[0] = 1;

    int hlit = 286, hdist = 30;
    while (hlit > 257 && litLen[hlit - 1] == 0) hlit--;
    while (hdist > 1 && dstLen[hdist - 1] == 0) hdist--;

    std::vector<unsigned char> all(litLen.begin(), litLen.begin() + hlit);
    all.insert(all.end(), dstLen.begin(), dstLen.begin() + hdist);
    std::vector<std::pair<int,int>> cl;
    clEncode(all, cl);
    std::vector<uint32_t> clFreq(19, 0);
    for (auto& p : cl) clFreq[p.first]++;
    std::vector<unsigned char> clLen;
    codeLengths(clFreq, 7, clLen);
    int hclen = 19;
    while (hclen > 4 && clLen[kClOrder[hclen - 1]] == 0) hclen--;

    // Cost the three shapes and pick. Fixed Huffman is often smaller than
    // dynamic for a short input, where the header outweighs what it saves.
    std::vector<unsigned char> fixLit(288), fixDst(30, 5);
    for (int i = 0;   i < 144; i++) fixLit[i] = 8;
    for (int i = 144; i < 256; i++) fixLit[i] = 9;
    for (int i = 256; i < 280; i++) fixLit[i] = 7;
    for (int i = 280; i < 288; i++) fixLit[i] = 8;

    long long dynBits = 3 + 5 + 5 + 4 + 3LL * hclen;
    for (auto& p : cl) dynBits += clLen[p.first] + (p.first == 16 ? 2 : p.first == 17 ? 3 : p.first == 18 ? 7 : 0);
    dynBits += tokenCost(toks, litLen, dstLen);
    long long fixBits = 3 + tokenCost(toks, fixLit, fixDst);
    long long stoBits = 8LL * (in.size() + 5 * ((in.size() + 65534) / 65535));

    BitOut b;
    if (stoBits <= dynBits && stoBits <= fixBits) { emitStored(b); out += b.out; return; }

    if (fixBits < dynBits) {
        b.put(1, 1);
        b.put(1, 2);
        std::vector<uint32_t> fixLitCode, fixDstCode;
        canonical(fixLit, fixLitCode);
        canonical(fixDst, fixDstCode);
        emitTokens(b, toks, fixLit, fixLitCode, fixDst, fixDstCode);
    }
    else {
        b.put(1, 1);
        b.put(2, 2);
        b.put(hlit - 257, 5);
        b.put(hdist - 1, 5);
        b.put(hclen - 4, 4);
        for (int i = 0; i < hclen; i++) b.put(clLen[kClOrder[i]], 3);
        std::vector<uint32_t> clCode;
        canonical(clLen, clCode);
        for (auto& p : cl) {
            b.putRev(clCode[p.first], clLen[p.first]);
            if (p.first == 16) b.put(p.second, 2);
            else if (p.first == 17) b.put(p.second, 3);
            else if (p.first == 18) b.put(p.second, 7);
        }
        std::vector<uint32_t> litCode, dstCode;
        canonical(litLen, litCode);
        canonical(dstLen, dstCode);
        emitTokens(b, toks, litLen, litCode, dstLen, dstCode);
    }
    b.align();
    out += b.out;
}

} // namespace

uint32_t zlibCrc32(const std::string& in, uint32_t init) {
    const uint32_t* t = crcTable();
    uint32_t c = ~init;
    for (unsigned char ch : in) c = t[(c ^ ch) & 0xFF] ^ (c >> 8);
    return ~c;
}

uint32_t zlibAdler32(const std::string& in, uint32_t init) {
    uint32_t s1 = init & 0xFFFF, s2 = (init >> 16) & 0xFFFF;
    for (unsigned char ch : in) { s1 = (s1 + ch) % 65521; s2 = (s2 + s1) % 65521; }
    return (s2 << 16) | s1;
}

std::string zlibDeflate(const std::string& in, int level, ZlibFormat fmt) {
    if (level < -1 || level > 9) zdie("compress: compression level must be between -1 and 9");
    std::string out;
    if (fmt == ZlibFormat::Zlib) {
        // CMF/FLG: deflate, a 32 KB window, no preset dictionary, and the two
        // bytes read as a big-endian multiple of 31.
        unsigned cmf = 0x78;
        unsigned flevel = level <= 1 ? 0u : level <= 5 ? 1u : level == 6 || level == -1 ? 2u : 3u;
        unsigned flg = flevel << 6;
        flg |= 31 - ((cmf << 8 | flg) % 31);
        out += (char)cmf;
        out += (char)flg;
    }
    else if (fmt == ZlibFormat::Gzip) {
        static const char hdr[10] = {(char)0x1F, (char)0x8B, 8, 0, 0, 0, 0, 0, 0, (char)0xFF};
        out.append(hdr, 10);
    }
    deflateBlocks(in, level, out);
    if (fmt == ZlibFormat::Zlib) {
        uint32_t a = zlibAdler32(in);
        for (int i = 3; i >= 0; i--) out += (char)((a >> (i * 8)) & 0xFF);   // big-endian
    }
    else if (fmt == ZlibFormat::Gzip) {
        uint32_t c = zlibCrc32(in);
        for (int i = 0; i < 4; i++) out += (char)((c >> (i * 8)) & 0xFF);    // little-endian
        uint32_t n = (uint32_t)(in.size() & 0xFFFFFFFFu);
        for (int i = 0; i < 4; i++) out += (char)((n >> (i * 8)) & 0xFF);
    }
    return out;
}

std::string zlibInflate(const std::string& in, ZlibFormat fmt) {
    const uint8_t* p = (const uint8_t*)in.data();
    const uint8_t* end = p + in.size();
    uint32_t wantSum = 0, wantLen = 0;
    bool haveSum = false;

    if (fmt == ZlibFormat::Zlib) {
        if (in.size() < 6) zdie("uncompress: a zlib stream is at least six bytes");
        unsigned cmf = p[0], flg = p[1];
        if ((cmf & 0x0F) != 8) zdie("uncompress: not a deflate stream (compression method is not 8)");
        if ((cmf >> 4) > 7) zdie("uncompress: the window is larger than 32 KB, which deflate does not define");
        if (((cmf << 8) | flg) % 31) zdie("uncompress: the zlib header check bits do not add up");
        if (flg & 0x20) zdie("uncompress: the stream needs a preset dictionary, which is not supported");
        p += 2;
        for (int i = 0; i < 4; i++) wantSum = (wantSum << 8) | end[-4 + i];
        end -= 4;
        haveSum = true;
    }
    else if (fmt == ZlibFormat::Gzip) {
        if (in.size() < 18) zdie("uncompress: a gzip stream is at least eighteen bytes");
        if (p[0] != 0x1F || p[1] != 0x8B) zdie("uncompress: not a gzip stream (bad magic)");
        if (p[2] != 8) zdie("uncompress: gzip compression method is not deflate");
        unsigned flg = p[3];
        if (flg & 0xE0) zdie("uncompress: gzip reserved header flags are set");
        p += 10;
        if (flg & 0x04) {                                  // FEXTRA
            if (end - p < 2) zdie("uncompress: gzip extra field is truncated");
            unsigned xlen = p[0] | (unsigned)p[1] << 8;
            p += 2;
            if ((size_t)(end - p) < xlen) zdie("uncompress: gzip extra field runs past the end");
            p += xlen;
        }
        if (flg & 0x08) { while (p < end && *p) p++; if (p == end) zdie("uncompress: gzip filename is unterminated"); p++; }
        if (flg & 0x10) { while (p < end && *p) p++; if (p == end) zdie("uncompress: gzip comment is unterminated"); p++; }
        if (flg & 0x02) { if (end - p < 2) zdie("uncompress: gzip header CRC is truncated"); p += 2; }
        if (end - p < 8) zdie("uncompress: gzip trailer is missing");
        for (int i = 0; i < 4; i++) wantSum |= (uint32_t)end[-8 + i] << (i * 8);
        for (int i = 0; i < 4; i++) wantLen |= (uint32_t)end[-4 + i] << (i * 8);
        end -= 8;
        haveSum = true;
    }
    if (p > end) zdie("uncompress: the header is longer than the stream");

    std::string out;
    BitIn b{p, end};
    inflateRaw(b, out);

    if (haveSum) {
        uint32_t got = fmt == ZlibFormat::Zlib ? zlibAdler32(out) : zlibCrc32(out);
        if (got != wantSum)
            zdie(fmt == ZlibFormat::Zlib ? "uncompress: the Adler-32 checksum does not match"
                                         : "uncompress: the CRC-32 checksum does not match");
        if (fmt == ZlibFormat::Gzip && (uint32_t)(out.size() & 0xFFFFFFFFu) != wantLen)
            zdie("uncompress: the gzip length field does not match what was decoded");
    }
    return out;
}

} // namespace rakupp
