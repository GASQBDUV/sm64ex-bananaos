#!/bin/bash
# PORTMASTER: sm64ex-alo.zip, Super Mario 64 Two Screens.sh

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/sm64ex-alo"
CONFDIR="$GAMEDIR/conf"

mkdir -p "$CONFDIR"
cd $GAMEDIR
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1
$ESUDO chmod +x "$GAMEDIR/sm64.us.f3dex2e.2screens"

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

$GPTOKEYB "sm64.us.f3dex2e.2screens" &
pm_platform_helper "$GAMEDIR/sm64.us.f3dex2e.2screens"
# run-port switched the bottom backlight off; this port uses both panels
. /usr/libexec/banana/one-panel
one_panel_dark top
one_panel_restore
./sm64.us.f3dex2e.2screens --savepath ./conf/ --fullscreen

pm_finish
