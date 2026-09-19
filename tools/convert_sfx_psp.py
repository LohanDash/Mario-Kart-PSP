from pathlib import Path
from array import array
import math
import random
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/_vendor"))
import miniaudio

SOURCE = ROOT / "data/sfx/source"
OUTPUT = ROOT / "data/sfx/psp"
OUTPUT.mkdir(parents=True, exist_ok=True)

FILES = {
    "menu_move.pcm": SOURCE / "481296/next_option.wav",
    "menu_confirm.pcm": SOURCE / "481296/confirm.wav",
    "menu_back.pcm": SOURCE / "481296/back1.wav",
    "race_start.pcm": SOURCE / "481296/start_race.wav",
    "engine.pcm": SOURCE / "398285/Engine Sounds/engine0.wav",
    "cannon.pcm": SOURCE / "481427/pipeplaza_pipe.wav",
    "landing.pcm": SOURCE / "481427/tarttop_bounce1.wav",
    "wall_hit.pcm": SOURCE / "481427/tarttop_splat.wav",
}

for index in range(11):
    FILES[f"mario_{index}.pcm"] = SOURCE / f"394015/Mario/Ingame/00BD_{index:04X}.wav"

for name, source in FILES.items():
    decoded = miniaudio.decode_file(
        str(source), output_format=miniaudio.SampleFormat.SIGNED16,
        nchannels=1, sample_rate=44100)
    samples = array("h", decoded.samples)
    active = [i for i, sample in enumerate(samples) if abs(sample) > 100]
    if active:
        padding = 4410
        end = min(len(samples), active[-1] + padding)
        samples = samples[:end]
    destination = OUTPUT / name
    destination.write_bytes(samples.tobytes())
    print(f"{source.name} -> {destination.name} ({destination.stat().st_size} bytes)")

# The public MKDS packs do not contain the tyre slide or mini-turbo samples.
# Create short DS-like placeholders so drift still has immediate audible
# feedback; they can later be replaced by exact SDAT sequence renders without
# changing the game code.
rng = random.Random(0x4D4B4453)
filtered = 0.0
skid = array("h")
for i in range(int(44100 * 0.16)):
    filtered = filtered * 0.82 + rng.uniform(-1.0, 1.0) * 0.18
    envelope = min(1.0, i / 300.0) * min(1.0, (len(range(int(44100 * 0.16))) - i) / 500.0)
    skid.append(int(filtered * envelope * 10500))
(OUTPUT / "drift.pcm").write_bytes(skid.tobytes())

turbo = array("h")
length = int(44100 * 0.28)
for i in range(length):
    t = i / 44100.0
    frequency = 420.0 + 950.0 * (i / length)
    envelope = min(1.0, i / 220.0) * max(0.0, 1.0 - i / length)
    turbo.append(int(math.sin(2.0 * math.pi * frequency * t) * envelope * 12000))
(OUTPUT / "drift_boost.pcm").write_bytes(turbo.tobytes())


# Course cues that are absent from the loose community WAV packs.  They are
# deliberately tiny, mono DS-style sounds: bright square/bell partials remain
# clear beneath the streamed music and cost very little PSP memory.
RATE = 44100


def write_synth(name, duration, sample_fn):
    count = int(RATE * duration)
    pcm = array("h")
    for i in range(count):
        value = int(sample_fn(i / RATE, i, count))
        pcm.append(max(-32768, min(32767, value)))
    destination = OUTPUT / name
    destination.write_bytes(pcm.tobytes())
    print(f"synth -> {destination.name} ({destination.stat().st_size} bytes)")


def note_cue(notes, step, decay=5.0, amplitude=12500, square_mix=0.12):
    def sample(t, _i, _count):
        note_index = min(int(t / step), len(notes) - 1)
        local = t - note_index * step
        frequency = notes[note_index]
        attack = min(1.0, local / 0.008)
        envelope = attack * math.exp(-decay * local)
        phase = 2.0 * math.pi * frequency * local
        sine = math.sin(phase) + 0.30 * math.sin(phase * 2.0)
        square = 1.0 if math.sin(phase) >= 0.0 else -1.0
        return (sine * (1.0 - square_mix) + square * square_mix) * envelope * amplitude
    return sample


write_synth("hop.pcm", 0.16,
    lambda t, _i, count: math.sin(2.0 * math.pi * (250.0 * t + 1250.0 * t * t)) *
    math.sin(math.pi * min(1.0, t / (count / RATE))) * 9000)

write_synth("pinball_launch.pcm", 0.62,
    lambda t, _i, _count: (
        math.sin(2.0 * math.pi * (190.0 * t + 1050.0 * t * t)) * 0.70 +
        math.sin(2.0 * math.pi * (380.0 * t + 1900.0 * t * t)) * 0.30) *
        min(1.0, t / 0.012) * math.exp(-2.4 * t) * 14500)

write_synth("pinball_dash.pcm", 0.34,
    lambda t, _i, _count: (
        math.sin(2.0 * math.pi * (310.0 + 920.0 * t) * t) * 0.72 +
        math.sin(2.0 * math.pi * (620.0 + 1550.0 * t) * t) * 0.28) *
        (0.45 + 0.55 * max(0.0, math.sin(2.0 * math.pi * 19.0 * t))) *
        math.sin(math.pi * min(1.0, t / 0.34)) * 12000)

write_synth("pinball_bumper.pcm", 0.26,
    lambda t, _i, _count: (
        math.sin(2.0 * math.pi * 880.0 * t) +
        0.52 * math.sin(2.0 * math.pi * 1760.0 * t) +
        0.24 * math.sin(2.0 * math.pi * 2637.0 * t)) *
        min(1.0, t / 0.003) * math.exp(-15.0 * t) * 9000)

flipper_rng = random.Random(0xF11F3E)
write_synth("pinball_flipper.pcm", 0.14,
    lambda t, _i, _count: (
        math.sin(2.0 * math.pi * 185.0 * t) * math.exp(-22.0 * t) * 0.72 +
        flipper_rng.uniform(-1.0, 1.0) * math.exp(-55.0 * t) * 0.38) * 15500)

write_synth("pinball_ball_hit.pcm", 0.22,
    lambda t, _i, _count: (
        math.sin(2.0 * math.pi * 430.0 * t) +
        0.62 * math.sin(2.0 * math.pi * 705.0 * t) +
        0.34 * math.sin(2.0 * math.pi * 1115.0 * t)) *
        min(1.0, t / 0.002) * math.exp(-18.0 * t) * 7600)

exit_rng = random.Random(0xBA11A057)
write_synth("pinball_exit.pcm", 0.42,
    lambda t, _i, _count: (
        math.sin(2.0 * math.pi * (720.0 * t - 520.0 * t * t)) * 0.58 +
        exit_rng.uniform(-1.0, 1.0) * (0.18 + 0.22 * math.sin(2.0 * math.pi * 13.0 * t))) *
        min(1.0, t / 0.004) * math.exp(-6.5 * t) * 13000)

# Race and Luigi's Mansion cues.  The environmental effects are intentionally
# non-musical so they cannot turn into the stray "piano notes" that used to
# occur when an incorrect sequence-bank sound was requested.
write_synth("lap.pcm", 0.48, note_cue([659.25, 880.0], 0.20,
    decay=6.0, amplitude=10500, square_mix=0.06))
write_synth("race_finish.pcm", 0.82, note_cue([523.25, 659.25, 783.99, 1046.5], 0.18,
    decay=4.5, amplitude=10500, square_mix=0.08))

tree_rng = random.Random(0x7EED)
tree_filtered = [0.0]
def mansion_tree_sample(t, _i, _count):
    tree_filtered[0] = tree_filtered[0] * 0.90 + tree_rng.uniform(-1.0, 1.0) * 0.10
    thud = math.sin(2.0 * math.pi * (92.0 - 35.0 * t) * t) * math.exp(-15.0 * t)
    bark = tree_filtered[0] * math.exp(-8.0 * t)
    return (0.72 * thud + 0.55 * bark) * 14500
write_synth("mansion_tree.pcm", 0.30, mansion_tree_sample)

chandelier_rng = random.Random(0xC4A1D)
write_synth("mansion_chandelier.pcm", 0.62,
    lambda t, _i, _count: (
        math.sin(2.0 * math.pi * (145.0 - 58.0 * t) * t) * 0.62 +
        chandelier_rng.uniform(-1.0, 1.0) * 0.12) *
        min(1.0, t / 0.025) * math.exp(-3.7 * t) * 10200)

boo_rng = random.Random(0xB00)
boo_filtered = [0.0]
def mansion_boo_sample(t, _i, _count):
    boo_filtered[0] = boo_filtered[0] * 0.965 + boo_rng.uniform(-1.0, 1.0) * 0.035
    voice = math.sin(2.0 * math.pi * (310.0 - 95.0 * t + 12.0 * math.sin(9.0 * t)) * t)
    envelope = min(1.0, t / 0.08) * max(0.0, 1.0 - t / 0.90)
    return (voice * 0.40 + boo_filtered[0] * 0.95) * envelope * 9200
write_synth("mansion_boo.pcm", 0.90, mansion_boo_sample)

picture_rng = random.Random(0x91C7)
write_synth("mansion_picture.pcm", 0.24,
    lambda t, i, _count: (
        picture_rng.uniform(-1.0, 1.0) * math.exp(-18.0 * (t % 0.075)) *
        (1.0 if (i // int(RATE * 0.075)) < 3 else 0.0) * 10500))
