/***
 * $("torrent") -- share folders with the BitTorrent protocol, without a server.
 *
 *     { makeTorrent, Torrent, download, seed } from $("torrent");
 *
 *     var[Torrent] t = makeTorrent("my-folder", "my-folder", DEFAULT_TRACKERS, []);
 *     print(t.infoHash);                       // what peers ask for
 *     seed([t], ["my-folder"], 6881, 0);       // share until stopped
 *
 *     // on another computer
 *     download(t.data, "copy-of-it", [], DEFAULT_OPTIONS);
 *
 * Peers find each other through public trackers (free servers that only
 * pass addresses around), on the local network (multicast, no internet
 * needed), or from a list of "host:port". When nobody shares a piece, the
 * torrent's web seeds (plain web addresses, e.g. a folder on GitHub) are used.
 * Every piece is checked with SHA-1 whoever sent it.
 *
 * The engine is C++ (torrent.cpp); riv uses it to share packages.
 */

{ _create, _infoHash, _matches, _download, _seed } from $("torrent");

// Free public trackers (UDP)
const[Str<>] DEFAULT_TRACKERS = [
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.stealth.si:80/announce",
    "udp://tracker.torrent.eu.org:451/announce",
    "udp://exodus.desync.com:6969/announce",
];

// trackers=0|1, lan=0|1, webseed=0|1, timeout=<seconds>, quiet=0|1
const[Str<>] DEFAULT_OPTIONS = ["trackers=1", "lan=1", "webseed=1", "timeout=60"];

class Torrent {
    var[Str] infoHash = "";    // hex: what peers ask for
    var[Str] data = "";        // the .torrent file, as base64
    var[Int] files = 0;
    var[Int] bytes = 0;
    var[Int] pieces = 0;

    func[Str] toStr() {
        return $"<Torrent {this.infoHash}: {this.files} file(s), {this.bytes} bytes, {this.pieces} piece(s)>";
    }
}

// A torrent describing a folder (text files counted with LF line endings).
func[Torrent] makeTorrent(folder[Str], name[Str], trackers[Str<>], webseeds[Str<>]) {
    var[Str<>] made = _create(folder, name, trackers, webseeds);
    var[Torrent] t = new Torrent();
    t.infoHash = made<0>;
    t.data = made<1>;
    t.files = Int(made<2>);
    t.bytes = Int(made<3>);
    t.pieces = Int(made<4>);
    return t;
}

// The infohash of a torrent (base64).
func[Str] infoHashOf(data[Str]) {
    return _infoHash(data);
}

// Does the folder hold exactly what the torrent describes?
func[Bul] folderMatches(folder[Str], data[Str]) {
    return _matches(folder, data);
}

// Downloads a torrent (base64) into a folder. Returns where the pieces came from.
func[Str] download(data[Str], target[Str], peers[Str<>], options[Str<>]) {
    return _download(data, target, peers, options);
}

// Shares folders; torrents<i> describes folders<i>. seconds = 0: until stopped.
func[Emp] seed(torrents[Torrent<>], folders[Str<>], port[Int], seconds[Int], options[Str<>] = []) {
    var[Str<>] data = [];
    for (var[Torrent] t in torrents) data.push(t.data);
    _seed(data, folders, port, options.isEmpty() ? DEFAULT_OPTIONS : options, seconds);
}
