#include "discid.h"
#include "sha1.h"
#include <cstdio>
#include <cstdlib>

namespace ln {

bool DiscToc::valid() const {
    if (offsets.empty() || lastTrack < firstTrack) return false;
    if (int(offsets.size()) != lastTrack - firstTrack + 1) return false;
    if (leadout <= offsets.back()) return false;
    for (size_t i = 1; i < offsets.size(); ++i)
        if (offsets[i] <= offsets[i - 1]) return false;
    return mbLastTrack() >= firstTrack;
}

int DiscToc::mbLastTrack() const {
    int last = lastTrack;
    for (int i = int(offsets.size()) - 1; i > 0; --i) {   // walk back over trailing data tracks
        if (i < int(isData.size()) && isData[i]) last = firstTrack + i - 1;
        else break;
    }
    return last;
}

int DiscToc::mbLeadout() const {
    int last = mbLastTrack();
    if (last == lastTrack) return leadout;
    int firstDataIdx = last - firstTrack + 1;
    return offsets[firstDataIdx] - 11400;
}

int DiscToc::audioTrackCount() const {
    int n = 0;
    for (size_t i = 0; i < offsets.size(); ++i)
        if (!(i < isData.size() && isData[i])) ++n;
    return n;
}

std::vector<int> DiscToc::audioTrackIndices() const {
    std::vector<int> idx;
    for (size_t i = 0; i < offsets.size(); ++i)
        if (!(i < isData.size() && isData[i])) idx.push_back(int(i));
    return idx;
}

int DiscToc::trackLengthMs(size_t i) const {
    if (i >= offsets.size()) return 0;
    int end = (i + 1 < offsets.size()) ? offsets[i + 1] : leadout;
    if (mbLastTrack() != lastTrack && int(i) == mbTrackCount() - 1) end = mbLeadout();
    return end > offsets[i] ? int((long long)(end - offsets[i]) * 1000 / 75) : 0;
}

static std::string mbBase64(const std::string& raw) {
    // Standard base64 alphabet with '+', '/', '=' replaced by '.', '_', '-'
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";
    const unsigned char* p = reinterpret_cast<const unsigned char*>(raw.data());
    std::string out;
    size_t i = 0;
    for (; i + 2 < raw.size(); i += 3) {
        uint32_t v = (p[i] << 16) | (p[i + 1] << 8) | p[i + 2];
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];  out += tbl[v & 63];
    }
    size_t rem = raw.size() - i;
    if (rem == 1) {
        uint32_t v = p[i] << 16;
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63]; out += "--";
    } else if (rem == 2) {
        uint32_t v = (p[i] << 16) | (p[i + 1] << 8);
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63]; out += tbl[(v >> 6) & 63]; out += '-';
    }
    return out;
}

std::string musicBrainzDiscId(const DiscToc& toc) {
    char buf[16];
    std::string s;
    int last = toc.mbLastTrack();
    std::snprintf(buf, sizeof buf, "%02X", toc.firstTrack);  s += buf;
    std::snprintf(buf, sizeof buf, "%02X", last);            s += buf;
    std::snprintf(buf, sizeof buf, "%08X", toc.mbLeadout()); s += buf;
    for (int t = 1; t <= 99; ++t) {
        int idx = t - toc.firstTrack, v = 0;
        if (t >= toc.firstTrack && t <= last && idx < int(toc.offsets.size())) v = toc.offsets[idx];
        std::snprintf(buf, sizeof buf, "%08X", v); s += buf;
    }
    return mbBase64(sha1(s));
}

std::string musicBrainzTocParam(const DiscToc& toc) {
    int last = toc.mbLastTrack();
    std::string s = std::to_string(toc.firstTrack) + "+" + std::to_string(last) + "+" +
                    std::to_string(toc.mbLeadout());
    for (int t = toc.firstTrack; t <= last; ++t) s += "+" + std::to_string(toc.offsets[t - toc.firstTrack]);
    return s;
}

uint32_t freedbDiscId(const DiscToc& toc) {
    auto digitSum = [](int n) { int s = 0; while (n > 0) { s += n % 10; n /= 10; } return s; };
    uint32_t n = 0;
    for (int off : toc.offsets) n += digitSum(off / 75);
    uint32_t t = uint32_t(toc.leadout / 75 - toc.offsets.front() / 75);
    return ((n % 255) << 24) | (t << 8) | uint32_t(toc.offsets.size() & 0xFF);
}

std::string freedbDiscIdHex(const DiscToc& toc) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08x", freedbDiscId(toc));
    return buf;
}

std::string gnudbQueryArgs(const DiscToc& toc) {
    std::string s = freedbDiscIdHex(toc) + "+" + std::to_string(toc.offsets.size());
    for (int off : toc.offsets) s += "+" + std::to_string(off);
    s += "+" + std::to_string(toc.leadout / 75);
    return s;
}

std::string describeToc(const DiscToc& toc) {
    std::string s = "tracks " + std::to_string(toc.firstTrack) + "-" + std::to_string(toc.lastTrack) +
                    ", lead-out " + std::to_string(toc.leadout) + ", offsets";
    for (size_t i = 0; i < toc.offsets.size(); ++i) {
        s += " " + std::to_string(toc.offsets[i]);
        if (i < toc.isData.size() && toc.isData[i]) s += "(data)";
    }
    return s;
}

std::string tocToString(const DiscToc& toc) {
    std::string s = std::to_string(toc.firstTrack) + " " + std::to_string(toc.lastTrack) + " " + std::to_string(toc.leadout);
    for (size_t i = 0; i < toc.offsets.size(); ++i) {
        s += " " + std::to_string(toc.offsets[i]);
        if (i < toc.isData.size() && toc.isData[i]) s += "d";
    }
    return s;
}

bool tocFromString(const std::string& s, DiscToc& out) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\r' || s[i] == '\n')) ++i;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\r' && s[j] != '\n') ++j;
        if (j > i) parts.push_back(s.substr(i, j - i));
        i = j;
    }
    if (parts.size() < 4) return false;
    auto num = [](const std::string& p, bool allowData, int& v, bool& data) {
        data = allowData && !p.empty() && p.back() == 'd';
        std::string digits = data ? p.substr(0, p.size() - 1) : p;
        if (digits.empty() || digits.size() > 9) return false;
        for (char c : digits) if (c < '0' || c > '9') return false;
        v = std::atoi(digits.c_str());
        return true;
    };
    DiscToc t;
    bool d = false;
    if (!num(parts[0], false, t.firstTrack, d) || !num(parts[1], false, t.lastTrack, d) || !num(parts[2], false, t.leadout, d))
        return false;
    for (size_t k = 3; k < parts.size(); ++k) {
        int off = 0;
        if (!num(parts[k], true, off, d)) return false;
        t.offsets.push_back(off);
        t.isData.push_back(d);
    }
    if (!t.valid()) return false;
    out = t;
    return true;
}

bool parseWmpMcdi(const std::string& mcdi, DiscToc& out) {
    std::vector<long> vals;
    size_t i = 0;
    while (i < mcdi.size()) {
        size_t j = mcdi.find('+', i);
        if (j == std::string::npos) j = mcdi.size();
        std::string part = mcdi.substr(i, j - i);
        if (!part.empty()) {
            char* end = nullptr;
            long v = std::strtol(part.c_str(), &end, 16);
            if (!end || *end != '\0') return false;
            vals.push_back(v);
        }
        i = j + 1;
    }
    if (vals.size() < 3) return false;
    long n = vals[0];
    if (n < 1 || n > 99 || long(vals.size()) != n + 2) return false;   // count, n offsets, lead-out
    DiscToc t;
    t.firstTrack = 1;
    t.lastTrack = int(n);
    long bias = (vals[1] == 0) ? 150 : 0;   // tolerate raw LBAs
    for (long k = 1; k <= n; ++k) { t.offsets.push_back(int(vals[k] + bias)); t.isData.push_back(false); }
    t.leadout = int(vals[n + 1] + bias);
    if (!t.valid()) return false;
    out = t;
    return true;
}

} // namespace ln
