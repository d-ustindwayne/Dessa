// $("torrent") -- a small BitTorrent engine for riv.
//
//   - Torrents (BEP 3): multi-file, made from a folder. Text files are read
//     with LF line endings, so a Windows checkout and a Linux one (or the copy
//     on GitHub) give the same torrent.
//   - Peers are found through UDP trackers (BEP 15), Local Service Discovery
//     on the local network (BEP 14) and a list given by the caller.
//   - Pieces travel with the peer wire protocol (BEP 3), checked with SHA-1.
//   - Web seeds (BEP 19): when no peer has a piece, the files are fetched
//     over HTTP(S) with curl (e.g. from GitHub) and checked the same way.
//
// Everything runs on one thread with select(); trackers are asked by a second
// thread while sharing.

#include "torrent.hpp"
#include "dcrypto/dcrypto.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace dessa::torrent {

namespace {

// ============================================================
// Sockets on Windows and Unix
// ============================================================

#ifdef _WIN32
using Socket = SOCKET;
const Socket kNoSocket = INVALID_SOCKET;
void closeSocket(Socket s) { closesocket(s); }
bool wouldBlock() { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
void nonBlocking(Socket s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
struct Startup {
    Startup() { WSADATA data; WSAStartup(MAKEWORD(2, 2), &data); }
};
#else
using Socket = int;
const Socket kNoSocket = -1;
void closeSocket(Socket s) { close(s); }
bool wouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS; }
void nonBlocking(Socket s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
struct Startup {};
#endif

void startNetwork() { static Startup startup; }

double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// IPv4 address of "host:port" (or host and port apart)
bool resolve(const std::string& host, int port, sockaddr_in& out) {
    addrinfo hints{}, *found = nullptr;
    hints.ai_family = AF_INET;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) return false;
    out = *reinterpret_cast<sockaddr_in*>(found->ai_addr);
    freeaddrinfo(found);
    return true;
}

std::string addressText(const sockaddr_in& a) {
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
    return std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
}

bool splitHostPort(const std::string& text, std::string& host, int& port) {
    size_t colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0) return false;
    host = text.substr(0, colon);
    try {
        port = std::stoi(text.substr(colon + 1));
    } catch (...) {
        return false;
    }
    return port > 0 && port < 65536;
}

void put32(std::string& out, std::uint32_t v) {
    for (int i = 3; i >= 0; --i) out += static_cast<char>((v >> (8 * i)) & 0xff);
}
void put64(std::string& out, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) out += static_cast<char>((v >> (8 * i)) & 0xff);
}
std::uint32_t get32(const std::string& s, size_t at) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v = (v << 8) | static_cast<std::uint8_t>(s[at + i]);
    return v;
}

std::string randomText(size_t n, const char* alphabet) {
    static std::random_device device;
    std::string out;
    size_t size = std::strlen(alphabet);
    for (size_t i = 0; i < n; ++i) out += alphabet[device() % size];
    return out;
}

const std::string& peerId() {
    static const std::string id = "-RV0010-" + randomText(12, "0123456789");
    return id;
}

std::string hexOf(const std::string& bytes) {
    return dcrypto::hexOf(dcrypto::Bytes(bytes.begin(), bytes.end()));
}

std::string sha1(const char* data, size_t size) {
    dcrypto::Bytes digest = dcrypto::sha1Bytes(dcrypto::Bytes(data, data + size));
    return std::string(digest.begin(), digest.end());
}

// ============================================================
// Bencoding
// ============================================================

struct Value {
    enum Kind { Integer, Text, List, Dict } kind = Text;
    long long number = 0;
    std::string text;
    std::vector<Value> list;
    std::vector<std::pair<std::string, Value>> dict;

    static Value of(long long n) { Value v; v.kind = Integer; v.number = n; return v; }
    static Value of(const std::string& s) { Value v; v.kind = Text; v.text = s; return v; }
    static Value listOf() { Value v; v.kind = List; return v; }
    static Value dictOf() { Value v; v.kind = Dict; return v; }

    const Value* get(const std::string& key) const {
        for (const auto& [k, v] : dict) if (k == key) return &v;
        return nullptr;
    }
};

void encode(const Value& v, std::string& out) {
    switch (v.kind) {
        case Value::Integer: out += "i" + std::to_string(v.number) + "e"; break;
        case Value::Text: out += std::to_string(v.text.size()) + ":" + v.text; break;
        case Value::List:
            out += "l";
            for (const auto& item : v.list) encode(item, out);
            out += "e";
            break;
        case Value::Dict: {
            auto entries = v.dict;
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            out += "d";
            for (const auto& [k, item] : entries) {
                out += std::to_string(k.size()) + ":" + k;
                encode(item, out);
            }
            out += "e";
            break;
        }
    }
}

std::string encode(const Value& v) {
    std::string out;
    encode(v, out);
    return out;
}

Value decode(const std::string& s, size_t& at, int depth = 0) {
    if (depth > 64 || at >= s.size()) fail("torrent: damaged data");
    char c = s[at];
    if (c == 'i') {
        size_t end = s.find('e', at);
        if (end == std::string::npos) fail("torrent: damaged data");
        Value v = Value::of(std::stoll(s.substr(at + 1, end - at - 1)));
        at = end + 1;
        return v;
    }
    if (c == 'l' || c == 'd') {
        Value v = c == 'l' ? Value::listOf() : Value::dictOf();
        ++at;
        while (at < s.size() && s[at] != 'e') {
            if (c == 'l') {
                v.list.push_back(decode(s, at, depth + 1));
            } else {
                Value key = decode(s, at, depth + 1);
                if (key.kind != Value::Text) fail("torrent: damaged data");
                v.dict.emplace_back(key.text, decode(s, at, depth + 1));
            }
        }
        if (at >= s.size()) fail("torrent: damaged data");
        ++at;
        return v;
    }
    if (c >= '0' && c <= '9') {
        size_t colon = s.find(':', at);
        if (colon == std::string::npos) fail("torrent: damaged data");
        size_t length = std::stoull(s.substr(at, colon - at));
        if (colon + 1 + length > s.size()) fail("torrent: damaged data");
        Value v = Value::of(s.substr(colon + 1, length));
        at = colon + 1 + length;
        return v;
    }
    fail("torrent: damaged data");
}

// ============================================================
// The content of a torrent
// ============================================================

struct FileEntry {
    std::string path;    // with '/', relative to the torrent's folder
    long long size = 0;
};

struct Content {
    std::string name;
    std::vector<FileEntry> files;
    long long pieceLength = 16384;
    std::string pieceHashes;   // 20 bytes per piece
    std::string infoHash;      // 20 bytes
    std::vector<std::string> trackers, webseeds;
    std::string data;          // every file one after the other

    long long total() const {
        long long sum = 0;
        for (const auto& f : files) sum += f.size;
        return sum;
    }
    size_t pieces() const { return pieceHashes.size() / 20; }
    long long pieceSize(size_t i) const {
        long long start = static_cast<long long>(i) * pieceLength;
        return std::min(pieceLength, total() - start);
    }
    bool pieceOk(size_t i) const {
        long long start = static_cast<long long>(i) * pieceLength;
        return sha1(data.data() + start, static_cast<size_t>(pieceSize(i))) == pieceHashes.substr(i * 20, 20);
    }
};

bool isText(const std::string& path) {
    static const std::set<std::string> text = {
        ".ds", ".dm", ".dx", ".dz", ".dsx", ".dxx", ".dzx", ".dds", ".dss", ".pkg", ".native", ".hpp", ".cpp", ".h",
        ".c", ".inc", ".mm", ".md", ".txt", ".json", ".js", ".css", ".html", ".dhtml", ".phl", ".py", ".sh", ".ps1",
        ".yml", ".yaml", ".csv", ".xml", ".svg", ".toml", ".ini", ".cfg"};
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text.count(ext) != 0;
}

std::string readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("torrent: can't read " + path.generic_string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// A folder's files, sorted, text files with LF line endings
void loadFolder(const std::string& folder, Content& c) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) fail("torrent: no folder '" + folder + "'");
    std::vector<std::string> paths;
    for (auto it = fs::recursive_directory_iterator(folder, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (name == "riv_modules" || (!name.empty() && name[0] == '.')) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec)) paths.push_back(fs::relative(it->path(), folder, ec).generic_string());
    }
    std::sort(paths.begin(), paths.end());
    c.files.clear();
    c.data.clear();
    for (const auto& p : paths) {
        std::string bytes = readBytes(fs::path(folder) / p);
        if (isText(p)) {
            std::string lf;
            lf.reserve(bytes.size());
            for (size_t i = 0; i < bytes.size(); ++i) {
                if (bytes[i] == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n') continue;
                lf += bytes[i];
            }
            bytes.swap(lf);
        }
        c.files.push_back({p, static_cast<long long>(bytes.size())});
        c.data += bytes;
    }
}

long long pieceLengthFor(long long total) {
    if (total <= 1024 * 1024) return 16 * 1024;
    if (total <= 32LL * 1024 * 1024) return 64 * 1024;
    return 256 * 1024;
}

Value infoOf(const Content& c) {
    Value info = Value::dictOf();
    Value files = Value::listOf();
    for (const auto& f : c.files) {
        Value file = Value::dictOf();
        file.dict.emplace_back("length", Value::of(f.size));
        Value parts = Value::listOf();
        std::stringstream path(f.path);
        std::string part;
        while (std::getline(path, part, '/')) parts.list.push_back(Value::of(part));
        file.dict.emplace_back("path", parts);
        files.list.push_back(file);
    }
    info.dict.emplace_back("files", files);
    info.dict.emplace_back("name", Value::of(c.name));
    info.dict.emplace_back("piece length", Value::of(c.pieceLength));
    info.dict.emplace_back("pieces", Value::of(c.pieceHashes));
    return info;
}

void hashPieces(Content& c) {
    c.pieceLength = pieceLengthFor(c.total());
    c.pieceHashes.clear();
    for (long long start = 0; start < static_cast<long long>(c.data.size()); start += c.pieceLength) {
        long long size = std::min<long long>(c.pieceLength, static_cast<long long>(c.data.size()) - start);
        c.pieceHashes += sha1(c.data.data() + start, static_cast<size_t>(size));
    }
    if (c.data.empty()) c.pieceHashes = sha1("", 0);   // an empty package still has one piece
}

std::string torrentOf(const Content& c) {
    Value top = Value::dictOf();
    if (!c.trackers.empty()) {
        top.dict.emplace_back("announce", Value::of(c.trackers.front()));
        Value tiers = Value::listOf();
        for (const auto& t : c.trackers) {
            Value tier = Value::listOf();
            tier.list.push_back(Value::of(t));
            tiers.list.push_back(tier);
        }
        top.dict.emplace_back("announce-list", tiers);
    }
    top.dict.emplace_back("created by", Value::of("riv (Dessa)"));
    top.dict.emplace_back("info", infoOf(c));
    if (!c.webseeds.empty()) {
        Value urls = Value::listOf();
        for (const auto& w : c.webseeds) urls.list.push_back(Value::of(w));
        top.dict.emplace_back("url-list", urls);
    }
    return encode(top);
}

bool safePart(const std::string& part) {
    return !part.empty() && part != "." && part != ".." && part.find('\\') == std::string::npos &&
           part.find(':') == std::string::npos;
}

Content parseTorrent(const std::string& base64) {
    dcrypto::Bytes raw = dcrypto::bytesOfBase64(base64, "torrent");
    std::string bytes(raw.begin(), raw.end());
    size_t at = 0;
    Value top = decode(bytes, at);
    if (top.kind != Value::Dict) fail("torrent: damaged data");
    // The infohash is the SHA-1 of the info dictionary exactly as written
    at = 1;
    std::string infoRaw;
    while (at < bytes.size() && bytes[at] != 'e') {
        Value key = decode(bytes, at);
        size_t start = at;
        decode(bytes, at);
        if (key.text == "info") infoRaw = bytes.substr(start, at - start);
    }
    const Value* info = top.get("info");
    if (!info || infoRaw.empty()) fail("torrent: no info in the torrent");
    Content c;
    c.infoHash = sha1(infoRaw.data(), infoRaw.size());
    const Value* name = info->get("name");
    const Value* length = info->get("piece length");
    const Value* pieces = info->get("pieces");
    if (!name || !length || !pieces || pieces->text.size() % 20 != 0) fail("torrent: damaged info");
    c.name = name->text;
    c.pieceLength = length->number;
    c.pieceHashes = pieces->text;
    if (const Value* files = info->get("files")) {
        for (const auto& f : files->list) {
            const Value* size = f.get("length");
            const Value* path = f.get("path");
            if (!size || !path) fail("torrent: damaged file list");
            std::string joined;
            for (const auto& part : path->list) {
                if (!safePart(part.text)) fail("torrent: unsafe file name '" + part.text + "'");
                joined += (joined.empty() ? "" : "/") + part.text;
            }
            c.files.push_back({joined, size->number});
        }
    } else if (const Value* size = info->get("length")) {
        if (!safePart(c.name)) fail("torrent: unsafe file name '" + c.name + "'");
        c.files.push_back({c.name, size->number});
    }
    if (const Value* list = top.get("announce-list")) {
        for (const auto& tier : list->list) for (const auto& t : tier.list) c.trackers.push_back(t.text);
    } else if (const Value* one = top.get("announce")) {
        c.trackers.push_back(one->text);
    }
    if (const Value* urls = top.get("url-list")) {
        if (urls->kind == Value::Text) c.webseeds.push_back(urls->text);
        for (const auto& u : urls->list) c.webseeds.push_back(u.text);
    }
    return c;
}

std::map<std::string, std::string> parseOptions(const List<Str>& options) {
    std::map<std::string, std::string> out = {{"trackers", "1"}, {"lan", "1"}, {"webseed", "1"}, {"timeout", "60"}, {"quiet", "0"}};
    for (const auto& o : options) {
        size_t eq = o.find('=');
        if (eq != std::string::npos) out[o.substr(0, eq)] = o.substr(eq + 1);
    }
    return out;
}

// ============================================================
// Finding peers: UDP trackers (BEP 15)
// ============================================================

bool waitReadable(Socket s, double seconds) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    timeval tv{};
    tv.tv_sec = static_cast<decltype(tv.tv_sec)>(seconds);
    tv.tv_usec = static_cast<decltype(tv.tv_usec)>((seconds - static_cast<double>(tv.tv_sec)) * 1e6);
    return select(static_cast<int>(s + 1), &set, nullptr, nullptr, &tv) > 0;
}

// Announces to one udp:// tracker; returns the peers it knows
std::vector<std::string> announce(const std::string& url, const std::string& infoHash, int port, long long left, int event, double timeout) {
    std::vector<std::string> peers;
    if (url.rfind("udp://", 0) != 0) return peers;   // only UDP trackers (no HTTP library here)
    std::string rest = url.substr(6);
    size_t slash = rest.find('/');
    std::string host;
    int trackerPort = 0;
    if (!splitHostPort(rest.substr(0, slash), host, trackerPort)) return peers;
    sockaddr_in address{};
    if (!resolve(host, trackerPort, address)) return peers;
    Socket s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == kNoSocket) return peers;
    auto exchange = [&](const std::string& request, std::string& reply, size_t minimum) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            sendto(s, request.data(), static_cast<int>(request.size()), 0, reinterpret_cast<sockaddr*>(&address), sizeof address);
            if (!waitReadable(s, timeout / 2)) continue;
            char buffer[4096];
            int got = static_cast<int>(recv(s, buffer, sizeof buffer, 0));
            if (got >= static_cast<int>(minimum)) {
                reply.assign(buffer, static_cast<size_t>(got));
                return true;
            }
        }
        return false;
    };
    std::uint32_t transaction = static_cast<std::uint32_t>(std::random_device()());
    std::string connect;
    put64(connect, 0x41727101980ULL);
    put32(connect, 0);
    put32(connect, transaction);
    std::string reply;
    if (exchange(connect, reply, 16) && get32(reply, 0) == 0 && get32(reply, 4) == transaction) {
        std::string request = reply.substr(8, 8);   // connection id
        put32(request, 1);
        put32(request, ++transaction);
        request += infoHash;
        request += peerId();
        put64(request, 0);
        put64(request, static_cast<std::uint64_t>(left));
        put64(request, 0);
        put32(request, static_cast<std::uint32_t>(event));
        put32(request, 0);
        put32(request, static_cast<std::uint32_t>(std::random_device()()));
        put32(request, 0xffffffffu);
        request += static_cast<char>((port >> 8) & 0xff);
        request += static_cast<char>(port & 0xff);
        if (exchange(request, reply, 20) && get32(reply, 0) == 1 && get32(reply, 4) == transaction) {
            for (size_t i = 20; i + 6 <= reply.size(); i += 6) {
                sockaddr_in peer{};
                peer.sin_family = AF_INET;
                std::memcpy(&peer.sin_addr, reply.data() + i, 4);
                std::memcpy(&peer.sin_port, reply.data() + i + 4, 2);
                if (peer.sin_port != 0) peers.push_back(addressText(peer));
            }
        }
    }
    closeSocket(s);
    return peers;
}

// ============================================================
// Finding peers on the local network (BEP 14)
// ============================================================

const char* kLsdGroup = "239.192.152.143";
const int kLsdPort = 6771;

Socket lsdSocket() {
    Socket s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == kNoSocket) return s;
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof on);
#ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&on), sizeof on);
#endif
    sockaddr_in any{};
    any.sin_family = AF_INET;
    any.sin_port = htons(kLsdPort);
    any.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, reinterpret_cast<sockaddr*>(&any), sizeof any) != 0) {
        closeSocket(s);
        return kNoSocket;
    }
    ip_mreq group{};
    inet_pton(AF_INET, kLsdGroup, &group.imr_multiaddr);
    group.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&group), sizeof group);
    int ttl = 1, loop = 1;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof ttl);
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, reinterpret_cast<const char*>(&loop), sizeof loop);
    nonBlocking(s);
    return s;
}

const std::string& lsdCookie() {
    static const std::string cookie = randomText(8, "abcdefghijklmnopqrstuvwxyz0123456789");
    return cookie;
}

void lsdAnnounce(Socket s, const std::vector<std::string>& infoHashes, int port) {
    std::string message = "BT-SEARCH * HTTP/1.1\r\nHost: " + std::string(kLsdGroup) + ":" + std::to_string(kLsdPort) +
                          "\r\nPort: " + std::to_string(port) + "\r\n";
    for (const auto& h : infoHashes) message += "Infohash: " + hexOf(h) + "\r\n";
    message += "cookie: " + lsdCookie() + "\r\n\r\n\r\n";
    sockaddr_in group{};
    group.sin_family = AF_INET;
    group.sin_port = htons(kLsdPort);
    inet_pton(AF_INET, kLsdGroup, &group.sin_addr);
    sendto(s, message.data(), static_cast<int>(message.size()), 0, reinterpret_cast<sockaddr*>(&group), sizeof group);
}

struct LsdMessage {
    std::string from;                 // "ip:port" from the Port header
    std::vector<std::string> hashes;  // hex, lower case
};

bool lsdReceive(Socket s, LsdMessage& out) {
    char buffer[2048];
    sockaddr_in sender{};
    socklen_t size = sizeof sender;
    int got = static_cast<int>(recvfrom(s, buffer, sizeof buffer - 1, 0, reinterpret_cast<sockaddr*>(&sender), &size));
    if (got <= 0) return false;
    std::string text(buffer, static_cast<size_t>(got));
    if (text.rfind("BT-SEARCH", 0) != 0) return false;
    std::stringstream lines(text);
    std::string line;
    int port = 0;
    std::string cookie;
    out.hashes.clear();
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon), value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(0, 1);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (key == "port") port = std::atoi(value.c_str());
        else if (key == "cookie") cookie = value;
        else if (key == "infohash") {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            out.hashes.push_back(value);
        }
    }
    if (cookie == lsdCookie()) return false;   // our own message
    sender.sin_port = htons(static_cast<std::uint16_t>(port));
    out.from = addressText(sender);
    return port > 0;
}

// ============================================================
// The peer wire protocol
// ============================================================

const int kBlock = 16384;

std::string handshake(const std::string& infoHash) {
    std::string h;
    h += static_cast<char>(19);
    h += "BitTorrent protocol";
    h += std::string(8, '\0');
    h += infoHash;
    h += peerId();
    return h;
}

std::string message(int id, const std::string& payload = "") {
    std::string m;
    put32(m, static_cast<std::uint32_t>(payload.size() + 1));
    m += static_cast<char>(id);
    return m + payload;
}

struct Connection {
    Socket socket = kNoSocket;
    std::string address;
    bool connecting = true;
    bool shookHands = false;
    std::string infoHash;      // the torrent this connection is about (sharing)
    std::string in, out;
    bool choked = true;
    std::vector<bool> has;
    std::set<std::pair<size_t, int>> requested;   // (piece, offset)
    double started = 0, lastHeard = 0;
    bool dead = false;
};

void flush(Connection& c) {
    while (!c.out.empty()) {
        int sent = static_cast<int>(send(c.socket, c.out.data(), static_cast<int>(std::min<size_t>(c.out.size(), 1 << 20)), 0));
        if (sent > 0) {
            c.out.erase(0, static_cast<size_t>(sent));
        } else {
            if (sent < 0 && wouldBlock()) return;
            c.dead = true;
            return;
        }
    }
}

bool readSome(Connection& c) {
    char buffer[65536];
    int got = static_cast<int>(recv(c.socket, buffer, sizeof buffer, 0));
    if (got > 0) {
        c.in.append(buffer, static_cast<size_t>(got));
        c.lastHeard = now();
        return true;
    }
    if (got < 0 && wouldBlock()) return false;
    c.dead = true;
    return false;
}

Socket connectTo(const std::string& peer) {
    std::string host;
    int port = 0;
    sockaddr_in address{};
    if (!splitHostPort(peer, host, port) || !resolve(host, port, address)) return kNoSocket;
    Socket s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == kNoSocket) return s;
    nonBlocking(s);
    if (connect(s, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 && !wouldBlock()) {
        closeSocket(s);
        return kNoSocket;
    }
    return s;
}

bool connected(Socket s) {
    int error = 0;
    socklen_t size = sizeof error;
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size);
    return error == 0;
}

// ============================================================
// Downloading
// ============================================================

std::string urlEncodePath(const std::string& path) {
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : path) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += digits[c >> 4];
            out += digits[c & 15];
        }
    }
    return out;
}

int runCommand(const std::string& command) {
#ifdef _WIN32
    return std::system(("\"" + command + "\"").c_str());
#else
    return std::system(command.c_str());
#endif
}

class Download {
public:
    Download(Content& c, const std::map<std::string, std::string>& options)
        : content(c), quiet(options.at("quiet") == "1") {
        content.data.assign(static_cast<size_t>(content.total()), '\0');
        done.assign(content.pieces(), false);
        blocks.resize(content.pieces());
        for (size_t i = 0; i < content.pieces(); ++i) {
            blocks[i].assign(static_cast<size_t>((content.pieceSize(i) + kBlock - 1) / kBlock), false);
        }
        // Empty pieces (an empty package) are done already
        for (size_t i = 0; i < content.pieces(); ++i) {
            if (content.pieceSize(i) <= 0) done[i] = true;
        }
    }

    size_t missing() const { return static_cast<size_t>(std::count(done.begin(), done.end(), false)); }

    // Downloads from the peers until done, stuck, or out of time
    void fromPeers(const std::vector<std::string>& peers, double timeout) {
        std::vector<Connection> connections;
        for (const auto& p : peers) {
            if (connections.size() >= 40) break;
            Socket s = connectTo(p);
            if (s == kNoSocket) continue;
            Connection c;
            c.socket = s;
            c.address = p;
            c.started = c.lastHeard = now();
            connections.push_back(std::move(c));
        }
        const double deadline = now() + timeout;
        while (missing() > 0 && now() < deadline) {
            bool alive = false;
            fd_set readable, writable, failed;
            FD_ZERO(&readable);
            FD_ZERO(&writable);
            FD_ZERO(&failed);
            Socket highest = 0;
            for (auto& c : connections) {
                if (c.dead) continue;
                alive = true;
                FD_SET(c.socket, &readable);
                FD_SET(c.socket, &failed);
                if (c.connecting || !c.out.empty()) FD_SET(c.socket, &writable);
                highest = std::max(highest, c.socket);
            }
            if (!alive) break;
            timeval tv{0, 200000};
            select(static_cast<int>(highest + 1), &readable, &writable, &failed, &tv);
            for (auto& c : connections) {
                if (c.dead) continue;
                if (c.connecting) {
                    if (FD_ISSET(c.socket, &failed) || (FD_ISSET(c.socket, &writable) && !connected(c.socket))) {
                        c.dead = true;
                    } else if (FD_ISSET(c.socket, &writable)) {
                        c.connecting = false;
                        c.out += handshake(content.infoHash);
                        c.lastHeard = now();
                    } else if (now() - c.started > 5) {
                        c.dead = true;   // nobody answers there
                    }
                    if (c.dead) continue;
                }
                if (FD_ISSET(c.socket, &readable)) {
                    while (readSome(c)) {}
                    handle(c);
                }
                if (!c.dead) request(c);
                if (!c.dead && !c.out.empty()) flush(c);
                if (!c.dead && now() - c.lastHeard > 30) c.dead = true;
                if (c.dead) forget(c);
            }
        }
        for (auto& c : connections) closeSocket(c.socket);
    }

    // Fetches the files that still have missing pieces from the web seeds
    void fromWebSeeds() {
        for (const auto& seed : content.webseeds) {
            if (missing() == 0) return;
            long long offset = 0;
            for (const auto& file : content.files) {
                long long first = offset / content.pieceLength;
                long long last = file.size == 0 ? first : (offset + file.size - 1) / content.pieceLength;
                bool needed = false;
                for (long long p = first; p <= last && p < static_cast<long long>(content.pieces()); ++p) needed = needed || !done[static_cast<size_t>(p)];
                if (needed && file.size > 0) {
                    std::string url = seed;
                    if (!url.empty() && url.back() == '/') url += urlEncodePath(content.name + "/" + file.path);
                    fs::path temp = fs::temp_directory_path() / ("riv_webseed_" + randomText(10, "abcdefghijklmnopqrstuvwxyz"));
                    std::string command = "curl -sfL --max-time 300 -o \"" + temp.string() + "\" \"" + url + "\"";
#ifndef _WIN32
                    command += " 2>/dev/null";
#endif
                    if (runCommand(command) == 0) {
                        std::error_code ec;
                        std::string bytes = readBytes(temp);
                        fs::remove(temp, ec);
                        if (static_cast<long long>(bytes.size()) == file.size) {
                            std::memcpy(content.data.data() + offset, bytes.data(), bytes.size());
                            for (long long p = first; p <= last; ++p) {
                                size_t i = static_cast<size_t>(p);
                                if (!done[i] && complete(i) && content.pieceOk(i)) {
                                    done[i] = true;
                                    ++fromWebSeed;
                                }
                            }
                        }
                    }
                }
                offset += file.size;
            }
            // Pieces that span files the web seed gave: check them all again
            for (size_t i = 0; i < content.pieces(); ++i) {
                if (!done[i] && content.pieceOk(i)) {
                    done[i] = true;
                    ++fromWebSeed;
                }
            }
        }
    }

    std::map<std::string, int> fromPeer;   // pieces per peer
    int fromWebSeed = 0;
    int damaged = 0;

private:
    Content& content;
    bool quiet;
    std::vector<bool> done;
    std::vector<std::vector<bool>> blocks;              // received
    std::map<std::pair<size_t, int>, double> asked;     // (piece, offset) -> when

    bool complete(size_t) const { return true; }   // a web seed sends whole files

    void forget(Connection& c) {
        for (const auto& r : c.requested) asked.erase(r);
        c.requested.clear();
    }

    void handle(Connection& c) {
        if (!c.shookHands) {
            if (c.in.size() < 68) return;
            if (c.in[0] != 19 || c.in.compare(1, 19, "BitTorrent protocol") != 0 || c.in.compare(28, 20, content.infoHash) != 0) {
                c.dead = true;
                return;
            }
            c.in.erase(0, 68);
            c.shookHands = true;
            c.has.assign(content.pieces(), false);
            c.out += message(2);   // interested
        }
        while (c.in.size() >= 4) {
            std::uint32_t length = get32(c.in, 0);
            if (length > (1u << 20) + 64) { c.dead = true; return; }
            if (c.in.size() < 4 + length) return;
            std::string m = c.in.substr(4, length);
            c.in.erase(0, 4 + length);
            if (length == 0) continue;   // keep-alive
            int id = static_cast<unsigned char>(m[0]);
            if (id == 0) {
                c.choked = true;
                forget(c);
            } else if (id == 1) {
                c.choked = false;
            } else if (id == 4 && m.size() >= 5) {
                size_t piece = get32(m, 1);
                if (piece < c.has.size()) c.has[piece] = true;
            } else if (id == 5) {
                for (size_t i = 0; i < c.has.size(); ++i) {
                    size_t byte = i / 8;
                    if (byte + 1 < m.size()) c.has[i] = (static_cast<unsigned char>(m[byte + 1]) >> (7 - i % 8)) & 1;
                }
            } else if (id == 7 && m.size() >= 9) {
                size_t piece = get32(m, 1);
                int offset = static_cast<int>(get32(m, 5));
                size_t size = m.size() - 9;
                c.requested.erase({piece, offset});
                asked.erase({piece, offset});
                if (piece >= content.pieces() || done[piece] || offset % kBlock != 0) continue;
                size_t block = static_cast<size_t>(offset / kBlock);
                long long start = static_cast<long long>(piece) * content.pieceLength + offset;
                if (block >= blocks[piece].size() || offset + static_cast<long long>(size) > content.pieceSize(piece)) continue;
                std::memcpy(content.data.data() + start, m.data() + 9, size);
                blocks[piece][block] = true;
                if (std::all_of(blocks[piece].begin(), blocks[piece].end(), [](bool b) { return b; })) {
                    if (content.pieceOk(piece)) {
                        done[piece] = true;
                        fromPeer[c.address]++;
                    } else {
                        ++damaged;   // the hash didn't match: ask again
                        std::fill(blocks[piece].begin(), blocks[piece].end(), false);
                    }
                }
            }
        }
    }

    void request(Connection& c) {
        if (!c.shookHands || c.choked) return;
        const double t = now();
        for (size_t p = 0; p < content.pieces() && c.requested.size() < 16; ++p) {
            if (done[p] || !c.has[p]) continue;
            for (size_t b = 0; b < blocks[p].size() && c.requested.size() < 16; ++b) {
                if (blocks[p][b]) continue;
                auto key = std::make_pair(p, static_cast<int>(b * kBlock));
                auto it = asked.find(key);
                if (it != asked.end() && t - it->second < 15) continue;   // asked someone already
                long long size = std::min<long long>(kBlock, content.pieceSize(p) - static_cast<long long>(b) * kBlock);
                std::string payload;
                put32(payload, static_cast<std::uint32_t>(p));
                put32(payload, static_cast<std::uint32_t>(b * kBlock));
                put32(payload, static_cast<std::uint32_t>(size));
                c.out += message(6, payload);
                c.requested.insert(key);
                asked[key] = t;
            }
        }
    }
};

std::vector<std::string> findPeers(const Content& c, const List<Str>& given, const std::map<std::string, std::string>& options, bool quiet) {
    std::vector<std::string> peers(given.begin(), given.end());
    if (options.at("lan") == "1") {
        Socket s = lsdSocket();
        if (s != kNoSocket) {
            lsdAnnounce(s, {c.infoHash}, 6881);
            const std::string wanted = hexOf(c.infoHash);
            double until = now() + 2.5, again = now() + 1;
            while (now() < until) {
                if (now() > again) { lsdAnnounce(s, {c.infoHash}, 6881); again = until + 1; }
                if (!waitReadable(s, 0.2)) continue;
                LsdMessage m;
                while (lsdReceive(s, m)) {
                    if (std::find(m.hashes.begin(), m.hashes.end(), wanted) != m.hashes.end() &&
                        std::find(peers.begin(), peers.end(), m.from) == peers.end()) {
                        peers.push_back(m.from);
                    }
                }
            }
            closeSocket(s);
        }
    }
    if (options.at("trackers") == "1") {
        for (const auto& t : c.trackers) {
            for (const auto& p : announce(t, c.infoHash, 6881, c.total(), 2, 3.0)) {
                if (std::find(peers.begin(), peers.end(), p) == peers.end()) peers.push_back(p);
            }
            if (peers.size() >= 30) break;
        }
    }
    if (!quiet && !peers.empty()) std::cout << "  " << peers.size() << " peer(s) found\n" << std::flush;
    return peers;
}

void writeFiles(const Content& c, const std::string& target) {
    std::error_code ec;
    long long offset = 0;
    for (const auto& f : c.files) {
        fs::path path = fs::path(target) / fs::path(f.path);
        fs::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary);
        if (!out) fail("torrent: can't write " + path.generic_string());
        out.write(c.data.data() + offset, static_cast<std::streamsize>(f.size));
        offset += f.size;
    }
}

// ============================================================
// Sharing
// ============================================================

class Seeder {
public:
    Seeder(std::map<std::string, Content> torrents, int port, const std::map<std::string, std::string>& options)
        : torrents(std::move(torrents)), wantedPort(port), options(options) {}

    void run(double seconds) {
        listener = listenOn(wantedPort);
        if (listener == kNoSocket) fail("torrent: can't listen on port " + std::to_string(wantedPort) + " (or the next 10)");
        std::cout << "Sharing " << torrents.size() << " torrent(s) on port " << port << ". Stop with Ctrl+C.\n" << std::flush;

        std::atomic<bool> stop{false};
        std::thread trackers;
        if (options.at("trackers") == "1") {
            trackers = std::thread([this, &stop] {
                double next = 0;
                while (!stop) {
                    if (now() >= next) {
                        int heard = 0;
                        for (const auto& [hash, c] : torrents) {
                            for (const auto& t : c.trackers) {
                                if (stop) break;
                                announce(t, hash, port, 0, 2, 4.0);
                                ++heard;
                            }
                        }
                        next = now() + 1800;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
            });
        }
        Socket lsd = options.at("lan") == "1" ? lsdSocket() : kNoSocket;
        std::vector<std::string> hashes;
        for (const auto& [hash, c] : torrents) hashes.push_back(hash);
        double nextLsd = 0, lastReply = 0;

        const double deadline = seconds > 0 ? now() + seconds : 1e300;
        std::vector<Connection> connections;
        while (now() < deadline) {
            if (lsd != kNoSocket && now() >= nextLsd) {
                lsdAnnounce(lsd, hashes, port);
                nextLsd = now() + 300;
            }
            fd_set readable, writable;
            FD_ZERO(&readable);
            FD_ZERO(&writable);
            FD_SET(listener, &readable);
            Socket highest = listener;
            if (lsd != kNoSocket) {
                FD_SET(lsd, &readable);
                highest = std::max(highest, lsd);
            }
            for (auto& c : connections) {
                FD_SET(c.socket, &readable);
                if (!c.out.empty()) FD_SET(c.socket, &writable);
                highest = std::max(highest, c.socket);
            }
            timeval tv{0, 250000};
            select(static_cast<int>(highest + 1), &readable, &writable, nullptr, &tv);

            if (FD_ISSET(listener, &readable) && connections.size() < 60) {
                sockaddr_in from{};
                socklen_t size = sizeof from;
                Socket s = accept(listener, reinterpret_cast<sockaddr*>(&from), &size);
                if (s != kNoSocket) {
                    nonBlocking(s);
                    Connection c;
                    c.socket = s;
                    c.address = addressText(from);
                    c.connecting = false;
                    c.started = c.lastHeard = now();
                    connections.push_back(std::move(c));
                }
            }
            if (lsd != kNoSocket && FD_ISSET(lsd, &readable)) {
                LsdMessage m;
                while (lsdReceive(lsd, m)) {
                    // Someone on this network looks for a torrent we have: answer
                    bool ours = false;
                    for (const auto& h : m.hashes) ours = ours || torrents.count(fromHex(h));
                    if (ours && now() - lastReply > 0.5) {
                        lsdAnnounce(lsd, hashes, port);
                        lastReply = now();
                    }
                }
            }
            for (auto& c : connections) {
                if (FD_ISSET(c.socket, &readable)) {
                    while (readSome(c)) {}
                    handle(c);
                }
                if (!c.dead && !c.out.empty()) flush(c);
                if (now() - c.lastHeard > 300) c.dead = true;
            }
            connections.erase(std::remove_if(connections.begin(), connections.end(), [](Connection& c) {
                if (c.dead) closeSocket(c.socket);
                return c.dead;
            }), connections.end());
        }
        stop = true;
        if (trackers.joinable()) trackers.join();
        for (auto& c : connections) closeSocket(c.socket);
        if (lsd != kNoSocket) closeSocket(lsd);
        closeSocket(listener);
    }

    int port = 0;

private:
    std::map<std::string, Content> torrents;   // infohash -> content
    int wantedPort;
    std::map<std::string, std::string> options;
    Socket listener = kNoSocket;
    std::map<std::string, int> served;

    static std::string fromHex(const std::string& hex) {
        std::string out;
        for (size_t i = 0; i + 1 < hex.size(); i += 2) out += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16));
        return out;
    }

    Socket listenOn(int first) {
        for (int p = first; p < first + 11; ++p) {
            Socket s = socket(AF_INET, SOCK_STREAM, 0);
            if (s == kNoSocket) continue;
#ifndef _WIN32
            int on = 1;
            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof on);
#endif
            sockaddr_in any{};
            any.sin_family = AF_INET;
            any.sin_port = htons(static_cast<std::uint16_t>(p));
            any.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(s, reinterpret_cast<sockaddr*>(&any), sizeof any) == 0 && listen(s, 32) == 0) {
                nonBlocking(s);
                port = p;
                return s;
            }
            closeSocket(s);
        }
        return kNoSocket;
    }

    void handle(Connection& c) {
        if (!c.shookHands) {
            if (c.in.size() < 68) return;
            if (c.in[0] != 19 || c.in.compare(1, 19, "BitTorrent protocol") != 0) { c.dead = true; return; }
            c.infoHash = c.in.substr(28, 20);
            auto it = torrents.find(c.infoHash);
            if (it == torrents.end()) { c.dead = true; return; }
            c.in.erase(0, 68);
            c.shookHands = true;
            const Content& content = it->second;
            c.out += handshake(c.infoHash);
            std::string bits((content.pieces() + 7) / 8, '\0');
            for (size_t i = 0; i < content.pieces(); ++i) bits[i / 8] = static_cast<char>(bits[i / 8] | (0x80 >> (i % 8)));
            c.out += message(5, bits);
            c.out += message(1);   // unchoked: everyone may download
        }
        const Content& content = torrents.at(c.infoHash);
        while (c.in.size() >= 4) {
            std::uint32_t length = get32(c.in, 0);
            if (length > 1024 * 1024) { c.dead = true; return; }
            if (c.in.size() < 4 + length) return;
            std::string m = c.in.substr(4, length);
            c.in.erase(0, 4 + length);
            if (length == 0) continue;
            int id = static_cast<unsigned char>(m[0]);
            if (id == 2) {
                c.out += message(1);
            } else if (id == 6 && m.size() >= 13) {
                size_t piece = get32(m, 1);
                long long begin = get32(m, 5), size = get32(m, 9);
                if (piece >= content.pieces() || size <= 0 || size > 131072 || begin + size > content.pieceSize(piece)) continue;
                long long start = static_cast<long long>(piece) * content.pieceLength + begin;
                std::string payload;
                put32(payload, static_cast<std::uint32_t>(piece));
                put32(payload, static_cast<std::uint32_t>(begin));
                payload.append(content.data, static_cast<size_t>(start), static_cast<size_t>(size));
                c.out += message(7, payload);
            }
        }
    }
};

} // namespace

// ============================================================
// The library
// ============================================================

List<Str> _create(const Str& folder, const Str& name, const List<Str>& trackers, const List<Str>& webseeds) {
    Content c;
    loadFolder(folder, c);
    c.name = name;
    c.trackers.assign(trackers.begin(), trackers.end());
    c.webseeds.assign(webseeds.begin(), webseeds.end());
    hashPieces(c);
    std::string torrent = torrentOf(c);
    c.infoHash = sha1(encode(infoOf(c)).data(), encode(infoOf(c)).size());
    return {hexOf(c.infoHash), dcrypto::base64Of(dcrypto::Bytes(torrent.begin(), torrent.end())), std::to_string(c.files.size()),
            std::to_string(c.total()), std::to_string(c.pieces())};
}

Str _infoHash(const Str& torrent) {
    return hexOf(parseTorrent(torrent).infoHash);
}

Bul _matches(const Str& folder, const Str& torrent) {
    Content expected = parseTorrent(torrent);
    Content here;
    loadFolder(folder, here);
    if (here.files.size() != expected.files.size()) return false;
    for (size_t i = 0; i < here.files.size(); ++i) {
        if (here.files[i].path != expected.files[i].path || here.files[i].size != expected.files[i].size) return false;
    }
    here.pieceLength = expected.pieceLength;
    here.pieceHashes = expected.pieceHashes;
    for (size_t i = 0; i < here.pieces(); ++i) {
        if (here.pieceSize(i) > 0 && !here.pieceOk(i)) return false;
    }
    return true;
}

Str _download(const Str& torrent, const Str& target, const List<Str>& peers, const List<Str>& options) {
    startNetwork();
    auto opts = parseOptions(options);
    const bool quiet = opts.at("quiet") == "1";
    Content c = parseTorrent(torrent);
    Download download(c, opts);
    double timeout = std::max(1.0, std::atof(opts.at("timeout").c_str()));
    if (download.missing() > 0) {
        std::vector<std::string> found = findPeers(c, peers, opts, quiet);
        if (!found.empty()) download.fromPeers(found, timeout);
    }
    if (download.missing() > 0 && opts.at("webseed") == "1" && !c.webseeds.empty()) {
        if (!quiet) std::cout << "  " << download.missing() << " piece(s) from the web seed\n" << std::flush;
        download.fromWebSeeds();
    }
    if (download.missing() > 0) {
        fail("no one is sharing " + c.name + " right now (" + std::to_string(download.missing()) + " of " +
             std::to_string(c.pieces()) + " piece(s) missing); try again later, or ask someone to run `riv seed`");
    }
    writeFiles(c, target);
    int fromPeers = 0;
    for (const auto& [peer, n] : download.fromPeer) fromPeers += n;
    std::string summary = std::to_string(c.pieces()) + " piece(s)";
    std::vector<std::string> parts;
    if (fromPeers > 0) parts.push_back(std::to_string(fromPeers) + " from " + std::to_string(download.fromPeer.size()) + " peer(s)");
    if (download.fromWebSeed > 0) parts.push_back(std::to_string(download.fromWebSeed) + " from the web seed");
    for (size_t i = 0; i < parts.size(); ++i) summary += (i ? ", " : ": ") + parts[i];
    if (download.damaged > 0) summary += ", " + std::to_string(download.damaged) + " damaged piece(s) caught and fetched again";
    return summary;
}

void _seed(const List<Str>& torrents, const List<Str>& folders, Int port, const List<Str>& options, Int seconds) {
    startNetwork();
    auto opts = parseOptions(options);
    std::map<std::string, Content> shared;
    for (size_t i = 0; i < torrents.size() && i < folders.size(); ++i) {
        Content c = parseTorrent(torrents[i]);
        Content here;
        loadFolder(folders[i], here);
        here.name = c.name;
        here.pieceLength = c.pieceLength;
        here.pieceHashes = c.pieceHashes;
        here.infoHash = c.infoHash;
        here.trackers = c.trackers;
        here.webseeds = c.webseeds;
        bool ok = here.files.size() == c.files.size();
        for (size_t f = 0; ok && f < here.files.size(); ++f) ok = here.files[f].path == c.files[f].path && here.files[f].size == c.files[f].size;
        for (size_t p = 0; ok && p < here.pieces(); ++p) ok = here.pieceSize(p) <= 0 || here.pieceOk(p);
        if (!ok) {
            std::cout << "  skipping " << folders[i] << ": its files changed since the torrent was made\n";
            continue;
        }
        shared.emplace(c.infoHash, std::move(here));
    }
    if (shared.empty()) fail("nothing to share");
    Seeder seeder(std::move(shared), static_cast<int>(port), opts);
    seeder.run(static_cast<double>(seconds));
}

} // namespace dessa::torrent
