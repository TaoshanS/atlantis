# Dumping your game

Atlantis SquareOff is built from two things you have to provide (see [game/README.md](../game/README.md)):

1. **The installed game folder** (`data/`, `maps/`, `sbso.exe`, …) — copy it into `game/`.
2. **The main SWF**, saved as `game/sbso_main.swf`.

The PC release wraps its main movie (the menus, profiles, map bus and cursor, plus fonts) inside `sbso.exe`, a Zinc projector protected
with Armadillo. The file on disk is encrypted, so the movie can only be read from memory while the game is running. `sbso_dump` does that.

## What you need

* The PC game (the 2008 WildGames / Big Fish Games release), installed.
* Windows to run it: a Windows PC, or a virtual machine. The dump has been done on **Windows XP SP3** under QEMU/UTM; newer Windows
  versions should work as long as the game starts. Wine (macOS/Linux) has not been tested.
* `sbso_dump.exe`: download it from the latest CI run (*Actions → Build → artifact `sbso_dump`*) or build it from
  [`tools/dump/sbso_dump.c`](../tools/dump/sbso_dump.c):

  ```sh
  i686-w64-mingw32-gcc -O2 -s -o sbso_dump.exe tools/dump/sbso_dump.c     # MinGW (Linux: apt install gcc-mingw-w64-i686, macOS: brew install mingw-w64)
  ```

## Steps

1. Start the game normally and wait until the **title screen** (PLAY / OPTIONS / PROFILES) is showing.
2. Run `sbso_dump.exe` from a command prompt in the folder where you want the file:

   ```
   sbso_dump.exe sbso_main.swf
   ```

   It looks through the memory of every `sbso.exe` process (the game starts two), finds the movie by its document class and checks it.
   Expected output:

   ```
   found the main SWF (493441 bytes, known release) in pid 196
   Saved sbso_main.swf. Copy it to game/sbso_main.swf in the port's folder.
   ```

   You can also start it first with `--wait 300`: it keeps retrying until the game has loaded the movie.
3. Close the game and copy `sbso_main.swf` to `game/sbso_main.swf` (from a VM: a shared folder, or a USB/ISO image).
4. Check: `python3 tools/game_data.py --check` should print `main SWF: sbso_main.swf (WildGames/Big Fish release (2008))`.

## Troubleshooting

* **"cannot open the process"**: run the dumper as the same user that started the game (or as administrator).
* **"not the known release"**: the movie was found by its class but its checksum differs (another release or a partial read).
  The build continues with a warning; if something looks wrong in the game, report the release you have.
* **Not found at all**: make sure the title screen is up, then run `sbso_dump.exe sbso_main.swf --all swfs` and report what it wrote:
  every AS3 movie in memory is saved to `swfs\`, and the main one is the ~480 KB file containing `sbso2`.
