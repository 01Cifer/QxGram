# QxGram — Desktop Messenger

QxGram is a custom desktop client for the Telegram messaging service, built
on top of the open-source [Telegram Desktop][telegram_desktop] code base and
the [Telegram API][telegram_api] with the [MTProto][telegram_proto] secure
protocol.

[![Release](https://img.shields.io/github/v/release/01Cifer/QxGram)](https://github.com/01Cifer/QxGram/releases)
[![License](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)

## Install on Windows

Open Windows PowerShell 5.1 or newer and run:

```powershell
iwr -useb https://raw.githubusercontent.com/01Cifer/QxGram/main/install.ps1 | iex
```

The installer downloads the latest Windows x64 release with progress, checks
its SHA-256 digest against GitHub, and installs it in `%LOCALAPPDATA%\QxGram`.
It then asks whether to create a desktop shortcut and whether to launch
QxGram. No administrator privileges are needed.

The installation uses a separate portable data folder at
`%LOCALAPPDATA%\QxGram\TelegramForcePortable\tdata`. Existing installations
and account data are not imported or overwritten. If the destination already
exists, the installer stops without changing it; use that client's built-in
updater instead. You can [review the installer](install.ps1) before running it.

## About

QxGram ships the familiar Telegram Desktop experience with its own release
channel:

* **Automatic updates.** The client checks the GitHub releases of this
  repository and installs signed update packages on its own. Only packages
  signed with the pinned QxGram update key and with a version newer than the
  running one are ever installed.
* **Windows 64-bit first.** Current releases target Windows 7+ (64 bit);
  macOS and Linux builds are possible from the same source.
* **Data in the working directory.** Account data lives in a `tdata` folder
  in the working directory, which may differ from the executable directory,
  and is preserved across automatic updates.

QxGram is an independent community build. It is not an official Telegram
client, and it is not affiliated with Telegram FZ-LLC.

## Downloads

Grab the latest release from the
[releases page](https://github.com/01Cifer/QxGram/releases):

* `QxGram-win64-<version>.zip` — portable archive. Unpack anywhere and run
  `QxGram.exe`.
* `td-update-win-x64-<version>` — signed update package, consumed
  automatically by the built-in updater. You normally never need to download
  it by hand.

Older QxGram builds that use the former GitHub repository cannot discover
updates here automatically. Back up `tdata` before moving to the new portable
archive, and keep the same working directory when starting the new client.

After adding more than six accounts, do not open the same `tdata` with
QxGram 7.4.5 or earlier: older versions cannot read that account list and
may overwrite local data. Back up `tdata` before an upgrade or downgrade.

## Building

See [docs/building-win.md](docs/building-win.md) for a full Windows
build guide (Visual Studio 2022, `../Libraries` layout). In short:

```
cmake -S . -B out
cmake --build out --config Release --target Telegram
```

The release signing and packaging workflow is part of the project's
internal tooling and is not published here.

## License

The source code is published under **GNU GPLv3** (with the OpenSSL
exception inherited from Telegram Desktop); see [LICENSE](LICENSE) and
[LEGAL](LEGAL). QxGram keeps every copyright notice of the upstream
Telegram Desktop project it is derived from.

[telegram_desktop]: https://github.com/telegramdesktop/tdesktop
[telegram_api]: https://core.telegram.org/api
[telegram_proto]: https://core.telegram.org/mtproto
