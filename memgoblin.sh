#!/bin/bash
cd /home/trite/playground/memgoblin
>/tmp/map_data.json

act() {
  local a="$1"

  if [[ "$a" -lt 40 ]]; then
    echo 0
  elif [[ "$a" -ge 40 && "$a" -lt 75 ]]; then
    echo 1
  elif [[ "$a" -ge 75 && "$a" -lt 103 ]]; then
    echo 2
  elif [[ "$a" -ge 103 && "$a" -lt 109 ]]; then
    echo 3
  else
    echo 4
  fi
}

printvars() {
  echo "Area ID: $areaid"
  echo "Act: $map_act"
  echo "Mapseed: $mapseed"
  printf "0x%x\n" "$mapseed"
}

runinwineprefix() {
  local a="$1"
  WINEDEBUG=-all WINEPREFIX="/home/trite/Games/battlenet" WINEFSYNC=1 /home/trite/.local/share/lutris/runners/wine/wine-ge-8-26-x86_64/bin/wine $a 2>/dev/null
  # Example for steam "add custom game" proton
  # WINEDEBUG=-all WINEPREFIX="/home/trite/.steam/steam/steamapps/compatdata/3827662210/pfx" WINEFSYNC=1 /home/trite/.steam/steam/steamapps/common/Proton\ -\ Experimental/files/bin/wine $a 2>/dev/null
}

mapdifficulty=1   # [0: Normal, 1: Nightmare, 2: Hell]
mapseed_area_pos=$(runinwineprefix "memgoblin.exe")
#echo "$mapseed_area"
mapseed=$(cut -d, -f1 <<< "$mapseed_area_pos")
areaid=$(cut -d, -f2 <<< "$mapseed_area_pos")
xpos=$(cut -d, -f3 <<< "$mapseed_area_pos")
ypos=$(cut -d, -f4 <<< "$mapseed_area_pos")

mapseed=$(echo "$mapseed" | tr -cd '[:digit:]')
areaid=$(echo "$areaid" | tr -cd '[:digit:]')
xpos=$(echo "$xpos" | tr -cd '[:digit:]')
ypos=$(echo "$ypos" | tr -cd '[:digit:]')

echo "$xpos"
echo "$ypos"

map_act=$(act "$areaid")

# Convert to hexadecimal and echo with '0x' prefix (optional)
if [[ -n "$mapseed_area_pos" ]]; then
  printvars
  # Call blacha and make json for us
  map_json=$(curl -s http://192.168.50.63:8899/v1/map/$mapseed/$mapdifficulty/$map_act/$areaid.json)

  # Save the JSON data to a file for draw_mapseed.py to read
  echo "$map_json" > /tmp/map_data.json

  # Run the draw_mapseed.py script to draw the map
  ./draw_mapseed /tmp/map_data.json $xpos $ypos
else
  echo "Error: map_seed, map_area_id or map_act is NULL."
  printvars
fi
