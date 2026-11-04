# sm64ex-alo with a second screen on BananaOS

Super Mario 64 (sm64ex-alo) on the Anbernic RG DS / RG DS Plus running BananaOS, with the bottom screen used as a map and status screen, like the Nintendo 3DS port:

- a map of the current level with Mario's position and direction
- lives, coins, stars, health and red coins (the top screen no longer draws them)
- the name of the music that is playing

The game runs on the top screen as usual. The bottom screen is drawn through DRM (a dumb buffer on DSI-1), the same way as in [uzdoom-bananaos](https://github.com/GASQBDUV/uzdoom-bananaos).

Not implemented compared with the 3DS port: the touch menu, transition animations and the credits screens. The Mario icon next to the life counter is a red circle.

## Credits

- The map and HUD pictures, the map layout and the idea come from **Epic0522's** [Super Mario 64 3DS port - Ultimate](https://github.com/Epic0522/Super-Mario-64-3ds-port---Ultimate), used with Epic0522's permission. Please credit Epic0522 if you reuse them.
- The maps themselves include work by alecpike (Bob-omb Battlefield), Turtle Boy (Lethal Lava Land and Whomp's Fortress), SlyP54 (Castle Grounds), Bruz (Castle Interior) and the SM64DS minimaps documented by Mario Wiki, as listed in that repository.
- [sm64ex-alo](https://github.com/AloUltraExt/sm64ex-alo) by AloUltraExt, based on sm64ex and the SM64 decompilation project.
- [BananaOS](https://github.com/JorreFog/BananaLinux) by JorreFog, and [PortMaster](https://portmaster.games).
- Super Mario 64 is owned by Nintendo. This is an unofficial fan project and contains no game data.

## Files

| File | What it is |
|---|---|
| `alo-hook.patch` | Small patch for sm64ex-alo: Makefile switch, one call per frame, no HUD on the top screen |
| `src/banana_ui.c` | Draws the bottom screen (320x240) from the game state |
| `src/banana_bottom.c` | Shows a picture on the DSI-1 panel through DRM |
| `tools/make_minimap_pack.py` | Packs the pictures of the 3DS port into `minimap.pack` |
| `Super Mario 64 Two Screens.sh` | PortMaster-style launcher |

## Build

You need your own legally obtained US ROM (`baserom.us.z64`). It is not part of this repository.

```sh
git clone https://github.com/AloUltraExt/sm64ex-alo
cd sm64ex-alo
git apply ../alo-hook.patch
cp ../src/banana_ui.c ../src/banana_bottom.c src/extras/
# cross build for aarch64 (Debian trixie chroot with aarch64 libs: libsdl2, libdrm, libgles)
make -j8 VERSION=us CROSS=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ \
     USE_GLES=1 HIGH_FPS_PC=1 BANANA_SECOND=1
```

The map pictures are not in this repository. This script downloads them from Epic0522's repository and packs them (needs git and python3 with Pillow):

```sh
sh tools/get_minimap_pack.sh minimap.pack   # if Pillow is only installed for your user: PYFLAGS= sh tools/...
```

## Install on the device

Copy the built binary as `sm64.us.f3dex2e.2screens` and `minimap.pack` into `ports/sm64ex-alo/`, and `Super Mario 64 Two Screens.sh` into `ports/`. Your original launcher and save data are untouched; both launchers share `conf/`.

`run-port` switches the bottom panel's backlight off for one-screen ports. The launcher turns it back on with `one_panel_dark top; one_panel_restore` from `/usr/libexec/banana/one-panel`.

Environment: `BANANA_SECOND=0` turns the second screen off, `SM64_MINIMAP_PACK` sets the path of the pack.

## Licence

My code (`src/`, the patch, the tool, the launcher) is GPL-3.0-or-later, see `LICENSE`. The pictures are Epic0522's and the map authors', not covered by that licence.
