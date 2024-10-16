#!/bin/bash
user=user123
cd /home/{$user}/playground/memgoblin
>/tmp/map_data.json
mapdifficulty=0   # [0: Normal, 1: Nightmare, 2: Hell]
mapact=0          # [0: ActI, 1: ActII, 2: ActIII, 3: ActIV, 4: Act5]
maplocation=8     # [0: Rogue Encampment ...] 101 = durance of hate 2, 4 = stony field

echo "Remember that compositor should be running for transparency, press alt + shift + f12 to restart compositor if its off (KDE)"

map_seed=$(WINEDEBUG=-all WINEPREFIX="/home/user/Games/battlenet" WINEFSYNC=1 /home/{$user}/.local/share/lutris/runners/wine/wine-ge-8-26-x86_64/bin/wine mapseed_reader.exe 2>/dev/null)

# Extract only digits from map_seed (in case it contains other characters)
map_seed_clean=$(echo "$map_seed" | tr -cd '[:digit:]')

# Convert to hexadecimal and echo with '0x' prefix (optional)
if [[ -n "$map_seed_clean" ]]; then
  echo "$map_seed_clean"
  printf "0x%x\n" "$map_seed_clean"

  # Call blacha and make json for us, launch firefox with blacha map for debug
  map_json=$(curl -s http://192.168.1.2:8899/v1/map/$map_seed_clean/$mapdifficulty/$mapact/$maplocation.json)
  firefox --new-tab "http://192.168.1.2:8899/?seed=$map_seed_clean&act=ActIII&difficulty=Nightmare&color=white#@0,0,z6.6931,p0,b0"

  # Save the JSON data to a file for draw_mapseed.py to read
  echo "$map_json" > /tmp/map_data.json

  # Run the draw_mapseed.py script to draw the map
  ./draw_mapseed /tmp/map_data.json
else
  echo "Error: map_seed does not contain a valid number."
fi
