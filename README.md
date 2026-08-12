# Strappy AI

Strappy AI is like Siri AI but for Jailbroken iPhones running iOS 4.3+ and 
Mac OS X Tiger 10.4+. Strappy scans your home folder for SQLite databases
and then exposes them to the LLM via read-only tools so that you can can ask
questions about your Text messages, Calendars, Notes, Music, and even third
party apps.

This README file mostly explains how to install Strappy but if you want the
full development story and a lot more demo videos, see my blog post:

[https://jeffburg.com/unenshittification/2026/08/11/Strappy.html](https://jeffburg.com/unenshittification/2026/08/11/Strappy.html)

## Demos

These videos are recorded on iOS 6 on my real iPhone 5 that I daily drive. It's
fully loaded with iCloud Mail, Contacts, Calendar as well as my RSS Feeds,
Podcasts, Music, iMessages, and LINE Messages. This gives Strappy plenty of
"meat" to work with in terms of database contents

| Personal Assistant | Coding Assistant |
|:---:|:---:|
| <a href="https://jeffburg.com/assets/images/unenshittification/strappy/03-vacation-gemini.mp4"><img src="https://jeffburg.com/assets/images/unenshittification/strappy/03-vacation-gemini-poster.png" alt="Strappy finding vacation details from personal databases" width="300"></a> | <a href="https://jeffburg.com/assets/images/unenshittification/strappy/05-pokedex-luna.mp4"><img src="https://jeffburg.com/assets/images/unenshittification/strappy/05-pokedex-luna-poster.png" alt="Strappy building a Pokédex app on an iPhone" width="300"></a> |
| Personal assistant activates the database tools so the LLM can run SQL queries on your databases to answer your question | Coding Assistant enables development tools so that Strappy can write, compile, install, and run new apps directly on your iPhone. |

## Modes

| Mode | Access |
|---|---|
| World Knowledge | Chat, memory, optional web search |
| Personal Assistant | Approved SQLite databases |
| Coding Assistant | Files and optional Bash |

Each session gets only its mode's tools.

## Coding Rules

In order to get modern networking and database on Retro Devices, I included 
modern-ish builds of libcurl, SQLite, and also cJSON because NSJSONSerialization
is actually kind of "new" if you can believe it. Because all of these libraries
are written in C, I decided to have Codex write almost all the code in C so
that the C "backend" remains portable and the Objective-C "frontend" is almost
100% UI code.

- Only strappy_cocoa.c can import Apple C libraries like CoreFoundation
- Only 2 Objective-C files can import C headers: StrappySession.m and FileScanner.m

Of course, I also had the hard rule that no errors or warnings are allowed from
the compiler and from the static analyzer. This is quite hard considering this
app is built with 3 different SDK's and spans 20 years of Mac OS X history
and 15 years of iOS history.

## Database Scanning

Strappy scans your home folder for SQLite databases and does its best to 
identify which application made the database. It then presents the databases
in a whitelisting UI so you can choose which databases the LLM should have
access to.

Protections are always enabled:

- Explicit database whitelist
- `SQLITE_OPEN_READONLY`
- SQLite authorizer blocking writes, PRAGMA, ATTACH, and transactions
- 100-row result limit

## Coding

Coding Assistant can read, write, edit, and run Bash commands in a selected 
directory. For the Mac, install Xcode or any other CLI based development
environment you like. For the iPhone use a package manager to install 
Clang and other development tools or use the one I made:
[Altivec toolchain](https://github.com/jeffreybergier/AltivecIntelligence/releases),

> **Warning:** There is no attempt at sandboxing this coding agent, so use
with caution.

## Compatibility

| Platform | Support | Tested | Package |
|---|---|---|---|
| iOS | 4.3+, armv7/arm64 | 6, 8, 15 | `.deb`; jailbreak required |
| Mac OS X/macOS | 10.4+, PPC/i386/x86_64/arm64 | 10.4–10.6, 10.8, 10.9, 10.14, 15 | zipped `.app` |

The Mac build is one quad-fat binary. Older Mac apps often predate SQLite, so
Personal Assistant may find less useful data there.

## Install

Requires an [OpenRouter](https://openrouter.ai/) API token.

### iOS

1. Jailbreak the device and enable SSH.
2. Download `Strappy-X.Y.Z-iOS.deb` from
   [Releases](https://github.com/jeffreybergier/Strappy-Cocoa/releases).
3. Install it:

```sh
scp Strappy-X.Y.Z-iOS.deb root@iphone-ip-address:~/strappy.deb
ssh root@iphone-ip-address "dpkg -i ~/strappy.deb && rm ~/strappy.deb"
```

Old SSH servers may require the compatibility options in the
[full installation guide](https://jeffburg.com/apps/retro-tech/unenshittification/2026/08/10/Strappy.html#ios).

### Mac

Download `Strappy-X.Y.Z-macOS.zip` from
[Releases](https://github.com/jeffreybergier/Strappy-Cocoa/releases), unzip it,
and open Strappy.

### First launch

In Preferences:

1. Save your OpenRouter token in keychain.
2. Select the models you want to whitelist
3. Scan for databases and then whitelist the ones you want
4. (Optional) Study databases to save tokens in future prompts

## Compile from Source

The project uses the my Docker-based retro development environment called
[Altivec Intelligence](https://github.com/jeffreybergier/AltivecIntelligence)

```sh
git clone https://github.com/jeffreybergier/Strappy-Cocoa.git
cd Strappy-Cocoa
docker compose pull
docker compose run --rm make clean release
```

Outputs:

- `source/iOS/build-release/Strappy.deb`
- `source/macOS/build-release/Strappy.zip`

(Optional) Run the Linux-Based Tests for the C Backend:

```sh
docker compose run --rm make clean test
```

## Source map

| Path | Purpose |
|---|---|
| `source/shared` | C core, tools, storage, prompts |
| `source/iOS` | UIKit app and `.deb` packaging |
| `source/macOS` | AppKit app and quad-fat packaging |
| `source/linux` | Portable C test harnesses |

Good starting points:

- [`strappy_responses.c`](source/shared/strappy_responses.c): agent loop
- [`strappy_tools.c`](source/shared/strappy_tools.c): tool validation and dispatch
- [`strappy_db.c`](source/shared/strappy_db.c): normalized SQLite storage
- [`AssistantSets.json`](source/shared/Resources/AssistantSets.json): modes and tool allowlists
- [`SystemPrompt.json`](source/shared/Resources/SystemPrompt.json): prompt structure

## Why Strappy Exists

I wrote Strappy for myself to use on my iPhone 5 running iOS 6 which is my 
daily-driver phone. This is a long story beyond the scope of this README.
But, technically, Strappy CAN exist because of the following:

- LLM's are surprisingly good at reading arbitrary data from arbitrary databases
- The OpenAI Responses API is a JSON-based API; You do not need a modern system to send, receive, and display JSON
- Vibe-Coding allows you to write in extremely high boilerplate languages like C and Objective-C with a lot less pain

## Status and license

Strappy is experimental personal software, not a hardened product. It is
[MIT licensed](LICENSE), 99% vibe-coded, and 100% compiled.
