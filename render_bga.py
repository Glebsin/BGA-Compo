import sys
import subprocess
from pathlib import Path
import tempfile
import shutil
import time

GREEN = "\033[92m"
CYAN = "\033[96m"
RESET = "\033[0m"

BGA_COMPO_NAME = "bga_compo_clean.exe"


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


def ask_mode():
    print("Select render mode:")
    print("1 - Lossless only")
    print("2 - Web only")
    print("3 - Lossless + Web")
    s = input("> ").strip()
    if s not in ("1", "2", "3"):
        die("Invalid choice")
    return s


def detect_encoder():
    try:
        out = subprocess.check_output(
            ["ffmpeg", "-hide_banner", "-encoders"],
            stderr=subprocess.DEVNULL,
            text=True
        )
    except:
        return ("libx264", ["-preset", "medium", "-crf", "18"])

    if "h264_nvenc" in out:
        return ("h264_nvenc", ["-preset", "p5"])
    if "h264_qsv" in out:
        return ("h264_qsv", [])
    if "h264_amf" in out:
        return ("h264_amf", [])
    return ("libx264", ["-preset", "medium", "-crf", "18"])


def draw_progress(pct):
    bar_len = 30
    filled = int(bar_len * pct / 100)
    bar = "█" * filled + "░" * (bar_len - filled)
    print(f"\r{CYAN}Progress:{RESET} [{bar}] {pct:3d}%", end="", flush=True)


def run_ffmpeg_with_progress(cmd, total_ms, start_pct, end_pct):
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1
    )

    last = start_pct

    for line in proc.stdout:
        if line.startswith("out_time_ms="):
            cur = int(line.split("=")[1])
            local = min(1.0, cur / total_ms)
            pct = start_pct + int(local * (end_pct - start_pct))
            if pct > last:
                for p in range(last + 1, pct + 1):
                    draw_progress(p)
                    time.sleep(0.003)
                last = pct
        elif line.startswith("progress=end"):
            break

    proc.wait()

    if last < end_pct:
        for p in range(last + 1, end_pct + 1):
            draw_progress(p)
            time.sleep(0.003)

    if proc.returncode != 0:
        die("ffmpeg failed")


if len(sys.argv) < 2:
    die("Drag and drop a .bms/.bme file onto this executable")

orig_bms_path = Path(sys.argv[1]).resolve()
if not orig_bms_path.exists():
    die("Input file not found")

chart_dir_name = orig_bms_path.parent.name

exe_dir = Path(sys.executable).parent
output_dir = exe_dir / chart_dir_name
output_dir.mkdir(exist_ok=True)

embedded_dir = Path(sys._MEIPASS) if hasattr(sys, "_MEIPASS") else exe_dir
embedded_bga = embedded_dir / BGA_COMPO_NAME
if not embedded_bga.exists():
    die(f"{BGA_COMPO_NAME} not found")

mode = ask_mode()
WIDTH, HEIGHT = ask_resolution()

lossless_out = output_dir / "out.avi"
web_out = output_dir / "out.mp4"

vcodec, vcodec_opts = detect_encoder()

with tempfile.TemporaryDirectory(prefix="bga_tmp_") as tmp:
    tmp = Path(tmp)

    chart_tmp = tmp / "chart"
    shutil.copytree(orig_bms_path.parent, chart_tmp)

    bms_tmp = chart_tmp / orig_bms_path.name

    bga_compo = tmp / BGA_COMPO_NAME
    shutil.copyfile(embedded_bga, bga_compo)

    video_raw = tmp / "video.rgb"
    audio_raw = tmp / "audio.pcm"

    with open(video_raw, "wb") as f:
        subprocess.run([str(bga_compo), "-v", str(bms_tmp)], stdout=f, check=True)

    with open(audio_raw, "wb") as f:
        subprocess.run([str(bga_compo), "-a", str(bms_tmp)], stdout=f, check=True)

    total_ms = int((audio_raw.stat().st_size / (44100 * 2 * 2)) * 1000)

    draw_progress(0)

    if mode == "1":
        run_ffmpeg_with_progress(
            [
                "ffmpeg", "-y",
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
                "-progress", "pipe:1",
                "-nostats",
                str(lossless_out)
            ],
            total_ms, 0, 100
        )
        print(f"\n{GREEN}Created:{RESET} {lossless_out}")

    if mode == "2":
        run_ffmpeg_with_progress(
            [
                "ffmpeg", "-y",
                "-f", "rawvideo",
                "-pixel_format", "rgb24",
                "-video_size", f"{WIDTH}x{HEIGHT}",
                "-framerate", "30",
                "-i", str(video_raw),
                "-f", "s16le",
                "-ar", "44100",
                "-ac", "2",
                "-i", str(audio_raw),
                "-c:v", vcodec,
                *vcodec_opts,
                "-pix_fmt", "yuv420p",
                "-movflags", "+faststart",
                "-c:a", "aac",
                "-b:a", "192k",
                "-progress", "pipe:1",
                "-nostats",
                str(web_out)
            ],
            total_ms, 0, 100
        )
        print(f"\n{GREEN}Created:{RESET} {web_out}")

    if mode == "3":
        run_ffmpeg_with_progress(
            [
                "ffmpeg", "-y",
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
                "-progress", "pipe:1",
                "-nostats",
                str(lossless_out)
            ],
            total_ms, 0, 50
        )

        run_ffmpeg_with_progress(
            [
                "ffmpeg", "-y",
                "-i", str(lossless_out),
                "-c:v", vcodec,
                *vcodec_opts,
                "-pix_fmt", "yuv420p",
                "-movflags", "+faststart",
                "-c:a", "aac",
                "-b:a", "192k",
                "-progress", "pipe:1",
                "-nostats",
                str(web_out)
            ],
            total_ms, 50, 100
        )

        print(f"\n{GREEN}Created:{RESET} {lossless_out}")
        print(f"{GREEN}Created:{RESET} {web_out}")

print(f"\n{GREEN}Done{RESET}")
print(f"{GREEN}Output folder:{RESET} {output_dir}")
input("Press Enter to exit...")
