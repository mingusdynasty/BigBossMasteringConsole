// Minimal WAV reading and writing for the bench (no JUCE): reads PCM 16/24/32-bit and float 32/64,
// plain or WAVE_FORMAT_EXTENSIBLE, mono or stereo (mono is copied to both channels; channels past
// the second are ignored). Writes 32-bit float stereo, so levels over 0 dBFS survive.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace bench
{
struct Audio
{
    double rate = 48000;
    std::vector<double> l, r;
    size_t size() const { return l.size(); }
};

namespace wavdetail
{
inline uint32_t u32 (const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
inline uint16_t u16 (const unsigned char* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
inline void put32 (std::vector<unsigned char>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back ((unsigned char) (x >> (8 * i))); }
inline void put16 (std::vector<unsigned char>& v, uint16_t x) { v.push_back ((unsigned char) x); v.push_back ((unsigned char) (x >> 8)); }
}

inline bool readWav (const std::string& path, Audio& out, std::string& error)
{
    using namespace wavdetail;
    FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr) { error = "cannot open " + path; return false; }
    std::vector<unsigned char> d;
    unsigned char buf[65536];
    for (size_t n; (n = std::fread (buf, 1, sizeof buf, f)) > 0;) d.insert (d.end(), buf, buf + n);
    std::fclose (f);
    if (d.size() < 12 || std::memcmp (d.data(), "RIFF", 4) != 0 || std::memcmp (d.data() + 8, "WAVE", 4) != 0)
        { error = path + " is not a WAV file"; return false; }

    int format = 0, channels = 0, bits = 0;
    const unsigned char* data = nullptr;
    size_t dataBytes = 0;
    for (size_t pos = 12; pos + 8 <= d.size();)
    {
        const uint32_t len = u32 (d.data() + pos + 4);
        const unsigned char* body = d.data() + pos + 8;
        const size_t avail = std::min<size_t> (len, d.size() - pos - 8);
        if (std::memcmp (d.data() + pos, "fmt ", 4) == 0 && avail >= 16)
        {
            format = u16 (body);
            channels = u16 (body + 2);
            out.rate = u32 (body + 4);
            bits = u16 (body + 14);
            if (format == 0xFFFE && avail >= 26) format = u16 (body + 24);   // the sub-format GUID's first two bytes
        }
        else if (std::memcmp (d.data() + pos, "data", 4) == 0) { data = body; dataBytes = avail; }
        pos += 8 + len + (len & 1);
    }
    if (data == nullptr || channels < 1) { error = path + ": no audio"; return false; }
    if (! ((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && (bits == 32 || bits == 64))))
        { error = path + ": unsupported sample format (" + std::to_string (format) + ", " + std::to_string (bits) + " bits)"; return false; }

    const int bytes = bits / 8, frame = bytes * channels;
    const size_t n = dataBytes / (size_t) frame;
    out.l.assign (n, 0.0);
    out.r.assign (n, 0.0);
    auto sample = [&] (const unsigned char* p) -> double
    {
        if (format == 3)
        {
            if (bits == 32) { float v; std::memcpy (&v, p, 4); return v; }
            double v; std::memcpy (&v, p, 8); return v;
        }
        if (bits == 16) return (int16_t) u16 (p) / 32768.0;
        if (bits == 24) { int32_t v = (int32_t) ((uint32_t) p[0] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 24); return (v >> 8) / 8388608.0; }
        return (int32_t) u32 (p) / 2147483648.0;
    };
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char* p = data + i * (size_t) frame;
        out.l[i] = sample (p);
        out.r[i] = channels > 1 ? sample (p + bytes) : out.l[i];
    }
    return true;
}

inline bool writeWav (const std::string& path, const Audio& a)
{
    using namespace wavdetail;
    std::vector<unsigned char> v;
    const uint32_t n = (uint32_t) a.size(), dataBytes = n * 8;
    v.insert (v.end(), { 'R', 'I', 'F', 'F' }); put32 (v, 36 + dataBytes);
    v.insert (v.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' }); put32 (v, 16);
    put16 (v, 3); put16 (v, 2); put32 (v, (uint32_t) a.rate); put32 (v, (uint32_t) a.rate * 8); put16 (v, 8); put16 (v, 32);
    v.insert (v.end(), { 'd', 'a', 't', 'a' }); put32 (v, dataBytes);
    v.reserve (v.size() + dataBytes);
    for (uint32_t i = 0; i < n; ++i)
        for (double x : { a.l[i], a.r[i] })
        {
            const float s = (float) x;
            unsigned char b[4];
            std::memcpy (b, &s, 4);
            v.insert (v.end(), b, b + 4);
        }
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite (v.data(), 1, v.size(), f) == v.size();
    return std::fclose (f) == 0 && ok;
}
} // namespace bench
