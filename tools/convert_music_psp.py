from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/_vendor"))
import miniaudio


music = ROOT / "data/music/psp"
for source in sorted(music.glob("*.mp3")):
    decoded = miniaudio.decode_file(
        str(source),
        output_format=miniaudio.SampleFormat.SIGNED16,
        nchannels=1,
        sample_rate=44100,
    )
    destination = source.with_suffix(".pcm")
    destination.write_bytes(decoded.samples.tobytes())
    print(f"{source.name} -> {destination.name} ({destination.stat().st_size} bytes)")
