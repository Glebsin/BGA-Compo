# BGA Compo
Working and compiled fork of - https://gist.github.com/ayuusweetfish/50764b32880710f9ec8b95de353a18fb for Windows (Huge W to [ayuusweetfish](https://gist.github.com/ayuusweetfish) for working code)

# HOW TO USE

1. Download [`BGA_compo.zip`](https://github.com/Glebsin/BGA-Compo/releases/download/2025.129.1/BGA_compo.zip) from Releases and unzip `BGA_compo` folder
2. Drag & Drop .bme/.bms file on `BGA_compo.exe`
3. Follow instructions in console

**Tested on Windows 11, ffmpeg should be installed and added to PATH**


# How to compile
bga_compo_clean.exe - `gcc bga_compo.c bmflat.c stb_vorbis.c -O2 -o bga_compo_clean.exe`

BGA_compo.exe - `python -m PyInstaller --onefile --name BGA_compo --add-binary "bga_compo_clean.exe;." --console render_bga.py`

<sub><sup>ᴀʟʟ ᴛʜɪꜱ ꜱʜɪᴛ ᴍᴀᴅᴇ ʙʏ ᴍᴇ ɪꜱ ᴅᴏɴᴇ ᴡɪᴛʜ ᴄʜᴀᴛɢᴘᴛ</sup></sub>
