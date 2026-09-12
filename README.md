<div align="center">

# Mousiki 🎵

![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)
![Language](https://img.shields.io/badge/Language-C++17-orange.svg)
![Platform](https://img.shields.io/badge/Platform-Linux_%7C_macOS_%7C_Android-brightgreen.svg)

Hey there! Welcome to **Mousiki**, a terminal music player built from the ground up for people who prefer control, simplicity, and a keyboard.

I created Mousiki because I wanted a fast, focused TUI (Terminal User Interface) without any unnecessary interface layers. It’s designed to be completely keyboard-driven and fully configurable while giving you rich features like spectrum visualizers, synced lyrics, and online streaming—all without ever leaving your terminal.

## Preview

![Mousiki Preview](./rawlook.png)

</div>
## ✨ Features

- **Local Music Playback:** Instantly browse and play your local music files.
- **Jellyfin Streaming:** Search and stream tracks straight from your Jellyfin media server via its native REST API (no yt-dlp, no Subsonic).
- **Synced Lyrics:** Real-time, word-by-word active lyrics highlighting fetched from your Jellyfin server with local `.lrc` sidecar caching.
- **Visualizers:** Real-time FFT spectrum, waveform rendering, and spinning disk art.
- **Queue Management:** Effortless queueing, shuffling, and repeating.
- **Highly Configurable:** Tweak colors, visualizer fluidity, animations, and hotkeys to match your exact workflow.

## 🚀 Supported Platforms

- **Native Support:** **Linux**, **macOS**, and **Android (Termux)**.
- **Unverified Support:** *Windows*. (Support for Windows is currently not verified because I don't have the hardware access needed to test and debug on that operating system. If you try it out and get it working, feel free to contribute!)

## 🛠️ Getting Started

## Default Keybindings

Configurable in `$HOME/.config/mousiki/config.txt`.

### Search & Playback
| Action | Keybinding | Description |
| :--- | :--- | :--- |
| **Local Search** | `/` | Filter and search local library |
| **Jellyfin Search** | `/s: <query>` | Search and stream music from your Jellyfin library |
| **Download Stream** | `y` | Save the currently streaming track into your music folder |
| **Play / Pause** | `p` (or `ENTER`) | Toggle playback |
| **Next / Previous Track** | `n` / `b` | Skip between songs |
| **Seek** | `ARROW_LEFT` / `ARROW_RIGHT` | Seek backward / forward |
| **Volume** | `1` / `2` | Decrease / Increase volume |
| **Shuffle / Repeat** | `m` / `r` | Toggle shuffle or repeat mode |

### Navigation & Queue
| Action | Keybinding | Description |
| :--- | :--- | :--- |
| **Navigate** | `ARROW_UP` / `ARROW_DOWN` | Move selection |
| **Switch Tabs/Cards** | `TAB` | Cycle between UI panels |
| **Add to Queue** | `a` | Enqueue selected track |
| **Remove from Queue** | `d` | Dequeue selected track |
| **Filter by Folder** | `f` | Apply folder filter |
| **Clear Filter** | `c` | Reset active search/filters |
| **Quit** | `q` | Exit application |


### Prerequisites & Installation

Mousiki relies on `ffmpeg` for decoding and `curl` for talking to your Jellyfin server. The easiest way to get started is by running the setup script on macOS (requires [Homebrew](https://brew.sh)), Debian-based Linux, or Termux:

```bash
# Clone the repository
git clone https://github.com/itzender5820/mousiki.git
cd mousiki

# Run the setup script (installs dependencies, sets up config, and builds the app)
bash setup.sh
```

If you're building manually, ensure you have `cmake`, a C++17 compiler, `ffmpeg`, and `curl` installed.

### Running the App

After a successful build, you can start the player with:
```bash
./build/mousiki
```

## ⚙️ Configuration

Your configuration file will be automatically generated at `$HOME/.config/mousiki/config.txt`. From there, you have complete freedom to customize Mousiki.

### Adding Custom Music Paths
You can easily tell Mousiki where to look for your music. Simply add multiple `LocalMusicPath` entries in your `config.txt`:

```ini
# Add as many custom paths as you need:
LocalMusicPath=/custom/path
LocalMusicPath=/home/user/Music
```

### Connecting to Your Jellyfin Server

Streaming and lyrics come from your Jellyfin server's native REST API. Add these to `$HOME/.config/mousiki/config.txt`:

```ini
# The base URL of your Jellyfin server
JellyfinServerUrl=http://your-server:8096

# A token from Jellyfin Dashboard -> API Keys
JellyfinApiKey=your-generated-api-key

# Set to false if your server has a proper https cert
JellyfinSkipCertCheck=true
```

Press `/`, type `s: <query>`, and Enter to search the Jellyfin library. Track audio is downloaded on demand and cached locally; press `y` to save the current track into your music folder. Lyrics fetched from the server are stored as `.lrc` files next to the (cached or saved) audio, so previously-fetched tracks show lyrics even when the server is unreachable.

## 🙏 Attribution & Dependencies

Mousiki stands on the shoulders of giants. A huge thank you to the developers behind these awesome open-source projects that make Mousiki tick:

- **[miniaudio](https://github.com/mackron/miniaudio):** An incredible single-file audio playback and capture library.
- **[kissfft](https://github.com/mborgerding/kissfft):** A wonderfully simple and lightweight real-input FFT library (powering the spectrum visualizer).
- **[Jellyfin](https://jellyfin.org/):** The open-source media server we stream from and pull synced lyrics from.
- **[FFmpeg](https://ffmpeg.org/):** The Swiss army knife of multimedia handling.
- **[curl](https://curl.se/):** Transfers used to talk to the Jellyfin API.

## 📜 License

This project is open-sourced under the [Apache License 2.0](LICENSE). 

## Star History

<a href="https://www.star-history.com/?repos=itzender5820%2Fmousiki&type=date&legend=top-left">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=itzender5820/mousiki&type=date&theme=dark&legend=top-left" />
    <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=itzender5820/mousiki&type=date&legend=top-left" />
    <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=itzender5820/mousiki&type=date&legend=top-left" />
  </picture>
</a>

---
*Crafted with ❤️ for the terminal by [itzender5820](https://github.com/itzender5820)*


<p align="center">
  <img
    src="https://raw.githubusercontent.com/mayankchaudhary26/Cool-Readme-ideas/refs/heads/master/data/trust%20me.gif"
    alt="Trust me"
  />
</p>
