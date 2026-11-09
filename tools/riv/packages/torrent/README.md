# torrent

Share folders with the BitTorrent protocol. riv uses it to pass packages around.

**Install:** Install with `riv add torrent`.

**Runs in:** Native programs only.

## Example

```dessa
$("torrent") { Torrent, makeTorrent, infoHashOf, DEFAULT_TRACKERS }

var[Torrent] t = makeTorrent("my-folder", "my-folder", DEFAULT_TRACKERS, <>);
print(t);
```

## What it has

| Name | Does |
|---|---|
| `makeTorrent(folder, name, trackers, webseeds)` | describes a folder as a `Torrent` |
| `infoHashOf(data)` | the identifier of a torrent's data |
| `folderMatches(folder, data)` | whether a folder holds exactly what the torrent says |
| `download(data, target, peers, options)` | fetches the files into `target` |
| `seed(torrents, folders, port, seconds, options)` | shares folders for that long |
| `DEFAULT_TRACKERS`, `DEFAULT_OPTIONS` | the usual trackers and options |

Peers are found through public trackers, on the local network, or from the list you
give. Web seeds fill in when no one shares a piece.
