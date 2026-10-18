// $("torrent") -- native part: sharing folders with the BitTorrent protocol.
// riv uses it to share packages without a server. The engine is in
// torrent.cpp, compiled once and linked in.
//
// Torrents travel through Dessa as base64 text (they are binary).
#pragma once
#include "dessa_runtime.hpp"

namespace dessa::torrent {

// A torrent for a folder: [infohash (hex), torrent (base64), files, bytes, pieces]
List<Str> _create(const Str& folder, const Str& name, const List<Str>& trackers, const List<Str>& webseeds);

// The infohash (hex) of a torrent
Str _infoHash(const Str& torrent);

// Does the folder hold exactly what the torrent describes?
Bul _matches(const Str& folder, const Str& torrent);

// Downloads a torrent into `target`. options: "trackers=0|1", "lan=0|1",
// "webseed=0|1", "timeout=<seconds>", "quiet=0|1". Peers: "host:port".
// Returns a one-line summary of where the pieces came from.
Str _download(const Str& torrent, const Str& target, const List<Str>& peers, const List<Str>& options);

// Shares folders (torrents[i] describes folders[i]) until `seconds` pass
// (0: until the program is stopped). options as for _download.
void _seed(const List<Str>& torrents, const List<Str>& folders, Int port, const List<Str>& options, Int seconds);

} // namespace dessa::torrent
