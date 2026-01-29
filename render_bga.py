import sys
import subprocess
from pathlib import Path
import tempfile
import shutil

GREEN = "\033[92m"
RESET = "\033[0m"

def die(msg):
    print(msg)
    input("Press Enter to exit...")
    sys.exit(1)

def ask_resolution():
    s = input("Enter BMP width and height (press Enter for 256x256): ").strip()
    if not s:
        return 256, 256
    try:
        w, h = map(int, s.lower().replace("x", " ").split())
        if w <= 0 or h <= 0:
            raise ValueError
        return w, h
    except:
        die("Invalid resolution format")

if len(sys.argv) < 2:
    die("Drag and drop a .bms/.bme file onto this executable")

bme_path = Path(sys.argv[1]).resolve()
if not bme_path.exists():
    die("Input file not found")

exe_dir = Path(sys.executable).parent

if hasattr(sys, "_MEIPASS"):
    embedded_dir = Path(sys._MEIPASS)
else:
    embedded_dir = exe_dir

embedded_bga = embedded_dir / "bga_compo.exe"
if not embedded_bga.exists():
    die("Embedded bga_compo.exe not found")

WIDTH, HEIGHT = ask_resolution()

lossless_out = exe_dir / "out.avi"
web_out = exe_dir / "out.mp4"

with tempfile.TemporaryDirectory(prefix="bga_tmp_") as tmp:
    tmp = Path(tmp)

    bga_compo = tmp / "bga_compo.exe"
    shutil.copyfile(embedded_bga, bga_compo)

    video_raw = tmp / "video.rgb"
    audio_raw = tmp / "audio.pcm"

    with open(video_raw, "wb") as f:
        subprocess.run(
            [str(bga_compo), "-v", str(bme_path)],
            stdout=f,
            check=True
        )

    with open(audio_raw, "wb") as f:
        subprocess.run(
            [str(bga_compo), "-a", str(bme_path)],
            stdout=f,
            check=True
        )

    subprocess.run([
        "ffmpeg",
        "-y",
        "-f", "rawvideo",
        "-pixel_format", "rgb24",
        "-video_size", f"{WIDTH}x{HEIGHT}",
        "-framerate", "30",
        "-i", str(video_raw),
        "-f", "s16le",
        "-ar", "44100",
        "-ac", "2",
        "-i", str(audio_raw),
        "-c:v", "huffyuv",
        "-c:a", "pcm_s16le",
        str(lossless_out)
    ], check=True)

print()
print(f"{GREEN}Lossless video created:{RESET}")
print(f"{GREEN}{lossless_out}{RESET}")
print()

resp = input("Convert video for web streaming? (Y = yes, Enter = exit): ").strip().lower()

if resp == "y":
    subprocess.run([
        "ffmpeg",
        "-y",
        "-i", str(lossless_out),
        "-c:v", "libx264",
        "-preset", "slow",
        "-crf", "18",
        "-profile:v", "high",
        "-level", "4.1",
        "-pix_fmt", "yuv420p",
        "-movflags", "+faststart",
        "-c:a", "aac",
        "-b:a", "192k",
        str(web_out)
    ], check=True)

    print()
    print(f"{GREEN}Web-compatible video created:{RESET}")
    print(f"{GREEN}{web_out}{RESET}")

input("\nPress Enter to exit...")
