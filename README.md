# Kindle LLM Chat

Native Kindle GTK+ 2.0 chat client in C++ (Meson) for jailbroken devices.

## Features

- ChatGPT-style sidebar for chat threads (create, switch, delete).
- Persistent chat history (stored as JSON per chat thread).
- Configurable backend URL/model/token for LM Studio and Ollama-style usage.
- Real-time streamed responses with Markdown re-rendered in the assistant bubble as tokens arrive.
- Context usage meter shown as percentage.
- Toggleable on-screen keyboard with XML layouts (kterm-style format).
- Top-right `X` button for graceful app exit.

## Markdown

Assistant and user bubbles are rendered with **[md4c](https://github.com/mity/md4c)** using **CommonMark** plus **GitHub-style** extensions (tables, task lists, strikethrough, permissive autolinks, and permissive `#Heading` without a required space after `#`). Paragraph breaks, soft vs hard line breaks, nested blockquotes, heading levels 1–6, and horizontal rules follow the same rules as the **[Markdown Guide — Basic Syntax](https://www.markdownguide.org/basic-syntax/)** (soft line breaks render as a space; “two spaces + newline” and related cases render as a hard line break; blank lines separate normal paragraphs).

**GTK+ 2 limits:** formatting is done with `GtkTextView` tags (fonts, weights, colors, scale), not a full HTML engine. Images are shown as emphasized **alt text** only; complex HTML blocks may look plain. Behavior should match familiar ChatGPT-style Markdown for typical assistant replies.

## Build (desktop smoke test)

Requirements:

- `meson`
- `ninja`
- `gtk+-2.0` development package
- `glib-2.0` development package
- `libcurl` development package

JSON is handled by **[cJSON](https://github.com/DaveGamble/cJSON)** in `third_party/cjson` (git submodule, built as a static library by Meson). No system json-glib package is required.

Install dependencies
```sh
sudo apt install build-essential meson ninja-build libgtk2.0-dev libcurl4-openssl-dev pkg-config
```

Clone this repository with submodules (`third_party/md4c`, `third_party/cjson`):

```sh
git clone --recursive https://github.com/mAd-DaWg/kindle_llm_chat.git
cd kindle_llm_chat
```

If you already cloned without submodules:

```sh
git submodule update --init --recursive
```

### Kindle cross-compile targets (koxtoolchain)

| TC        | Supported devices                    |
| --------- | ------------------------------------ |
| kindle    | Kindle 2, DX, DXg, 3                 |
| kindle5   | Kindle 4, Touch, PW1                |
| kindlepw2 | Kindle PW2 and newer on FW < 5.16.3 |
| kindlehf  | Any Kindle on FW >= 5.16.3          |

Build toolchain and sdk:
```sh
git clone --recursive --depth=1 https://github.com/koreader/koxtoolchain.git
git clone --recursive --depth=1 https://github.com/KindleModding/kindle-sdk.git

cd koxtoolchain
chmod +x ./gen-tc.sh
./gen-tc.sh kindlehf

cd ../kindle-sdk
chmod +x ./gen-sdk.sh
./gen-sdk.sh kindlehf
```

Take note of the `To cross-compile via Meson, use the meson-crosscompile.txt file at` message from the above command.

Build commands:

```sh
meson setup builddir
meson compile -C builddir
```

Run locally:

```sh
./builddir/kindle-llm-chat
```
Run with debug messages:
```sh
gdb -q -batch -ex run -ex bt --args ./builddir/kindle-llm-chat
```

Crosscompile for kindle using the toolchain, `<meson_crosscompile_path>` is typically `~/x-tools/arm-kindlehf-linux-gnueabihf/meson-crosscompile.txt` for `kindlehf`, but use the output from the sdk build above you were supposed to take note of.

```sh
meson setup --cross-file <meson_crosscompile_path> builddir_kindlehf
meson compile -C builddir_kindlehf
```

**Cross file:** use the `meson-crosscompile.txt` path printed at the end of `./gen-sdk.sh kindlehf` (for kindlehf it is usually `~/x-tools/arm-kindlehf-linux-gnueabihf/meson-crosscompile.txt`). Pass that file to `meson setup --cross-file …`; do not edit it unless your toolchain lives in a different directory.

## Runtime Configuration

Use the in-app **Settings** button to configure:

- Base URL
- Model
- API key (optional)
- Backend mode (`openai_compatible` or `ollama`)
- Context window size

Configuration is persisted in:

- `/mnt/us/extensions/kindle_llm_chat/data/config.json`

Chats are persisted in:

- `/mnt/us/extensions/kindle_llm_chat/data/index.json`
- `/mnt/us/extensions/kindle_llm_chat/data/chats/*.json`

## Kindle Packaging (KUAL/MRPI)

1. Build binary using your Kindle toolchain (see crosscompile commands above).
2. Place resulting binary at:
   - `builddir/kindle-llm-chat`
3. Create package artifact:

```sh
./tools/package_mrpi.sh
```

This produces:

- `dist/kindle-llm-chat-mrpi.tar.gz`

The archive contains `extensions/kindle_llm_chat/*` for deployment on Kindle USB storage root.

## KUAL Launcher

Launcher script:

- `kindle.pkg/bin/run.sh`

KUAL menu definition:

- `kindle.pkg/menu.json`

Keyboard layouts:

- `kindle.pkg/layouts/keyboard-200dpi.xml`
- `kindle.pkg/layouts/keyboard-300dpi.xml`

`run.sh` auto-selects layout based on Kindle DPI.

## Notes

- The network API flow is OpenAI-compatible streaming; Ollama mode uses `/api/chat`.
- If usage metadata is absent from the backend stream, context percentage remains based on available token counters only.
