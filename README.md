# Sung · QQ Music

A native third-party QQ Music client for Linux, built with C++20, Qt 6 and QML.
QQ Music is the only online music service. Local audio files remain supported.

## Features

- **Home**: QQ Music song recommendations, recommended playlists, new songs and new albums.
- **Search**: songs, albums, artists and playlists, plus standard QQ Music links.
- **Account library**: liked songs, created playlists and collected playlists, with pagination and refresh.
- **Account likes**: favorite and unfavorite QQ Music songs; failed writes preserve the previous state.
- **Login**: scan a QQ, WeChat or QQ Music QR code inside the app, or import a full QQMusicApi credential snapshot.
- **Playback**: authorized QQ Music MP3 audio, local files, offline replay cache, queue management, next-track preparation, gapless transitions and crossfade.
- **Local library**: import files and folders, local playlists and likes, smart playlists, history, M3U import/export and playlist snapshots.
- **Lyrics**: QQ Music lyrics, imported LRC files and local LRC sidecars.
- **Appearance**: Material UI, light/dark themes, adjustable accent colors, local animated artwork and an immersive player.
- **Desktop**: MPRIS media controls, notifications, system tray, background playback, mini player and audio output selection.

QQ Music integration uses [QQMusicApi](https://l-1124.github.io/QQMusicApi/), pinned to `qqmusic-api-python==0.8.2`. Stream availability depends on the signed-in account, subscription and region. QQ Music playback currently requests MP3 128 kbps. The app reports denied access and does not bypass account restrictions.

## Build and run

On Arch Linux / CachyOS:

```bash
sudo pacman -S --needed base-devel cmake ninja python ffmpeg qt6-base qt6-declarative qt6-multimedia qt6-svg qt6-wayland qt6-imageformats
./scripts/setup.sh
./scripts/build.sh
./scripts/run-qqmusic.sh
```

Other distributions need a C++20 compiler, CMake 3.24+, Ninja, Python 3.10+, FFmpeg and Qt 6.8+ development packages for Core, Concurrent, Gui, Quick, Qml, QuickControls2, Multimedia, Network, DBus, Svg and Widgets. Install image-format plugins for WebP covers. No browser or JavaScript runtime is needed for music resolution.

`run-qqmusic.sh` runs this checkout's `build/sung` with separate configuration, library and cache under `.dev-state/qqmusic/`. It uses the desktop's session bus so Caelestia and other MPRIS clients receive the current song, artwork and playback controls. The `qqmusic` instance owns a separate activation socket and `org.mpris.MediaPlayer2.sung.qqmusic` service, allowing it to coexist with an installed Sung. Running the script again restores the existing window. State persists between launches.

With a system tray available, closing either player window hides it and keeps music playing. Use the Sung tray icon to restore the window, control playback or quit. **Ctrl+Q** and MPRIS **Quit** always exit. **Settings → Playback → Keep playing when window closes** disables this behavior. If the desktop has no tray, closing the window exits normally. The `--isolated` flag is reserved for tests and disables desktop integration.

To use your normal profile, run `./scripts/run.sh`. To install this build per user, run `./scripts/install.sh`; the default destination is `~/.local/bin/sung`. The installer creates its own Python environment. Do not run it with sudo.

## QQ Music login

Choose **Sign in** on Home or either account library page, or open **Settings → Connections → Sign in to QQ Music**. Select QQ, WeChat or QQ Music, scan with the corresponding mobile app and confirm on your phone. Closing the dialog cancels the session. Use **New code** after expiry or a failed attempt.

The app saves credentials automatically in a separate owner-readable file. Credentials are excluded from library exports and track metadata. Sign in again when they expire, or choose **Remove QQ Music credentials** in Settings. Automatic credential renewal is not implemented.

To import credentials generated elsewhere, use **Settings → Connections → Import QQ Music credentials** and select the complete `credential.model_dump_json()` snapshot, including `encryptUin`. A snapshot containing only `musicid` and `musickey` can authorize playback but does not contain the account identifier needed for the account library.

The standalone `./scripts/qqmusic-login.sh` also supports QR login. Its default is mobile QQ; `--type wx` uses WeChat and `--type mobile` uses the QQ Music app. It saves `.dev-state/qqmusic/login/credential.json` with owner-only permissions and removes the temporary QR image when finished. Import that snapshot into the app if you use the standalone script.

The helper handles empty response cookies that break QQMusicApi 0.8.2's default QR transport.

## Music library

**Library → Liked songs** reads the current QQ Music account's favorites. Red-heart changes go to that account and update the UI after a successful response. **Library → Playlists** reads both created and collected playlists, displays their covers and track counts, and opens their QQ Music tracks. Account changes clear the previous account's lists and heart state. Requests are bounded to the first 5,000 entries.

Cloud playlists currently support browsing and playback. Creation, editing and the **Add to local playlist** action operate on local playlists under **Library → Local files → Playlists**. Original local likes remain under **Local files → Liked**.

QQ Music song, album, artist and playlist links on `y.qq.com` can be pasted into Search. Short share links are not supported yet. Other online providers and their saved links are unavailable; retained local library records are not converted into QQ Music IDs.

Use **Local files → +** or **Folders → Add folder** to import music. Folder scans are recursive, bounded and update automatically while the app runs. Missing files stay listed as unavailable. Album and artist views use local tags, including album artist and disc ordering. Local playlists support M3U import/export, smart rules, cover selection, duplicate cleanup and version restore.

Played QQ Music audio follows the cache budget in **Settings → Privacy & data**. Files above the 32 MiB temporary buffer limit stream directly. Local media is never copied into that cache. Artwork comes from QQ Music or local files; local animated covers can be selected in the Artwork dialog. No external cover-matching or lyric-fallback service is contacted.

## Development and verification

```bash
./scripts/test.sh
./scripts/verify.sh
```

`python3 tests/desktop_session.py --binary build-qq-ui/sung` verifies Caelestia MPRIS discovery, tray registration, background playback, mini-player close and MPRIS Quit against the real desktop. Build that binary with `-DSUNG_DIAGNOSTICS=ON` first. This native stage requires Caelestia and Hyprland 0.56, uses a temporary profile and an empty background workspace (95 by default), and never changes your account library.

The unit suite covers QQ Music adapter contracts, login, account changes, local media, playback transitions, cache behavior, artwork and MPRIS metadata. The UI verification stage uses a deterministic QQ Music fixture and an isolated profile. Its screenshots and logs are stored under the ignored `verification/` directory. Do not publish credentials or signed media URLs in reports.

Sources are licensed under the project's [MIT license](LICENSE); see [NOTICE](NOTICE) for runtime dependencies and third-party assets.
