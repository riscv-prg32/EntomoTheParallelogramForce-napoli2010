#!/usr/bin/env python3
"""Compose audio/audio.json: SID-like instruments and five tracker tunes.

Nothing is sampled. Every voice is one of the PRG32 procedural oscillators
(triangle, saw, pulse, noise) through its ADSR envelope and resonant
low-pass filter, so the AUDIO block holds only descriptors and events.

Stereo plan (pan -64 left .. +63 right). The tracker plays instrument n on
channel n, so the music owns channels 0-3 and their instruments carry the
stage position; the game plays the effects on channels 4-7 and pans each
one to where it happens on screen (see src/entomo.c, sfx()).

    ch 0  bass        pulse     centre
    ch 1  lead        saw       left   (mandolin-like pluck)
    ch 2  bells/arp   triangle  right
    ch 3  percussion  noise     far right (tammorra and tambourine)
    ch 4  shield hum  instrument 10, sustained while B is held
    ch 5  effects     instruments 8-13
    ch 6-7 the two cave echoes of an effect, thrown to opposite sides

The tunes are original, written here as text. A note is NAME[:TICKS]
("A4", "G#3:3"), "-" is a rest; a tick is the tracker's sixteenth note.
The title is a tarantella in A minor, counted in six.

Usage: tools/build_audio.py     (writes audio/audio.json, deterministic)
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TRI, SAW, PULSE, NOISE = 0, 1, 2, 3


def synth(wave, cutoff, resonance, width=8):
    return 0x8000 | ((resonance & 3) << 10) | ((cutoff & 15) << 6) | ((width & 15) << 2) | wave


def instrument(name, sample_id, volume, pan, attack, decay, sustain, release):
    return {"name": name, "sample_id": sample_id, "default_volume": volume, "default_pan": pan,
            "attack": attack, "decay": decay, "sustain": sustain, "release": release}


INSTRUMENTS = [
    instrument("bass", synth(PULSE, 6, 1, 5), 215, 0, 0, 96, 0, 20),
    instrument("lead", synth(SAW, 11, 1), 185, -30, 0, 84, 0, 30),
    instrument("bells", synth(TRI, 13, 0), 175, 32, 2, 130, 0, 60),
    instrument("percussion", synth(NOISE, 13, 0), 115, 46, 0, 28, 0, 10),
    instrument("spare4", synth(TRI, 10, 0), 150, 0, 0, 60, 0, 20),
    instrument("spare5", synth(TRI, 10, 0), 150, 0, 0, 60, 0, 20),
    instrument("spare6", synth(TRI, 10, 0), 150, 0, 0, 60, 0, 20),
    instrument("spare7", synth(TRI, 10, 0), 150, 0, 0, 60, 0, 20),
    instrument("scrub", synth(NOISE, 11, 2), 140, 0, 0, 22, 0, 8),
    instrument("blip", synth(PULSE, 14, 0, 8), 170, 0, 0, 46, 0, 20),
    instrument("shield_hum", synth(SAW, 4, 3), 150, 0, 34, 60, 150, 64),
    instrument("boing", synth(TRI, 10, 2), 200, 0, 0, 74, 0, 30),
    instrument("clank", synth(NOISE, 6, 3), 220, 0, 0, 62, 0, 30),
    instrument("drip", synth(TRI, 15, 0), 100, 0, 0, 36, 0, 20),
]

NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    step = NOTE[name[0]]
    rest = name[1:]
    if rest[0] == "#":
        step += 1
        rest = rest[1:]
    elif rest[0] == "b":
        step -= 1
        rest = rest[1:]
    return 12 * (int(rest) + 1) + step


def voice(text):
    """'A4 C5:2 - E5' -> [(start_tick, midi)], total ticks."""
    t, out = 0, []
    for token in text.split():
        name, _, length = token.partition(":")
        if name != "-":
            out.append((t, midi(name)))
        t += int(length or 1)
    return out, t


def track(tempo, voices, loop=True):
    """voices: {channel: text}. All voices must have the same length."""
    events, length = [], None
    for channel, text in voices.items():
        notes, ticks = voice(text)
        assert length in (None, ticks), f"channel {channel}: {ticks} ticks, expected {length}"
        length = ticks
        events += [(t, channel, note) for t, note in notes]
    events.sort()
    out = [{"delta": 0, "command": "SET_TEMPO", "arg0": tempo, "arg1": 0}]
    first = events[0][0]
    if first:
        out[0]["delta"] = first
    for i, (t, channel, note) in enumerate(events):
        nxt = events[i + 1][0] if i + 1 < len(events) else length
        assert nxt - t <= 255
        out.append({"delta": nxt - t, "command": "NOTE_ON", "arg0": channel, "arg1": note})
    if loop:
        out.append({"delta": 0, "command": "JUMP", "arg0": 1, "arg1": 0})
        assert first == 0, "a looping track must start on its first tick"
    else:
        out.append({"delta": 0, "command": "END", "arg0": 0, "arg1": 0})
    return {"events": out}


def bars(*items):
    return " ".join(items)


# Title: tarantella, six ticks to the bar (tempo counts the tick as a 16th).
TARANTELLA_LEAD = bars(
    "E5 E5 E5 E5 D5 C5", "B4 B4 B4 B4 C5 D5", "C5 C5 C5 C5 B4 A4", "G#4 E4 G#4 B4:3",
    "A4 C5 E5 A5 G5 E5", "F5 F5 F5 F5 E5 D5", "E5 C5 A4 B4 G#4 E4", "A4:2 E5 A4:3",
    "G5 G5 G5 G5 F5 E5", "F5 F5 F5 F5 E5 D5", "E5 E5 E5 E5 D5 C5", "D5 B4 G4 D5:3",
    "C5 E5 G5 C6 B5 G5", "A5 A5 A5 A5 G5 F5", "E5 C5 A4 B4 G#4 B4", "A4:2 C5 A4:3")
TARANTELLA_BASS = bars(
    "A2:3 E3:3", "E2:3 B2:3", "A2:3 E3:3", "E2:3 E3:3", "A2:3 E3:3", "D3:3 A2:3", "E2:3 E3:3", "A2:3 A2:3",
    "C3:3 G2:3", "D3:3 A2:3", "C3:3 G2:3", "G2:3 G3:3", "C3:3 G2:3", "F2:3 C3:3", "E2:3 E3:3", "A2:3 A2:3")
TARANTELLA_ARP = bars(
    "- C4 E4 - C4 E4", "- B3 E4 - B3 E4", "- C4 E4 - C4 E4", "- B3 D4 - G#3 B3",
    "- C4 E4 - C4 E4", "- D4 F4 - D4 F4", "- B3 E4 - G#3 D4", "- C4 E4 - A3:2",
    "- E4 G4 - E4 G4", "- D4 F4 - D4 F4", "- E4 G4 - E4 G4", "- D4 G4 - B3 D4",
    "- E4 G4 - E4 G4", "- F4 A4 - F4 A4", "- B3 E4 - G#3 D4", "- C4 E4 - A3:2")
TARANTELLA_PERC = " ".join(["C3 C7 C7 C4 C7 C7"] * 16)

# Surface patrol: D minor, sixteen ticks to the bar.
PATROL_BASS = bars(
    "D2:2 D2 - D3 - D2:2 F2:2 G2 - A2 C3 D3 -", "D2:2 D2 - D3 - D2:2 F2:2 G2 - A2 G2 F2 -",
    "Bb1:2 Bb1 - Bb2 - Bb1:2 D2:2 F2 - G2 F2 D2 -", "C2:2 C2 - C3 - C2:2 E2:2 G2 - A2 G2 E2 -",
    "D2:2 D2 - D3 - D2:2 F2:2 G2 - A2 C3 D3 -", "D2:2 D2 - D3 - D2:2 F2:2 G2 - A2 G2 F2 -",
    "Bb1:2 Bb1 - Bb2 - Bb1:2 D2:2 F2 - G2 F2 D2 -", "A1:2 A1 - A2 - A1:2 C#2:2 E2 - G2 E2 C#2 -")
PATROL_LEAD = bars(
    "A4:3 D5:3 F5:2 E5:3 D5:3 C5:2", "D5:6 -:2 A4:2 C5:2 D5:4",
    "F5:3 D5:3 Bb4:2 D5:3 F5:3 G5:2", "E5:6 -:2 C5:2 E5:2 G5:4",
    "A5:3 F5:3 D5:2 E5:3 F5:3 G5:2", "A5:6 -:2 F5:2 E5:2 D5:4",
    "Bb4:3 D5:3 F5:2 G5:3 F5:3 D5:2", "C#5:4 E5:4 A5:4 A4:4")
PATROL_ARP = bars(
    *["- D4 - F4 - A4 - F4 - D4 - F4 - A4 - D5"] * 2, "- D4 - F4 - Bb4 - F4 - D4 - F4 - Bb4 - D5",
    "- C4 - E4 - G4 - E4 - C4 - E4 - G4 - C5", *["- D4 - F4 - A4 - F4 - D4 - F4 - A4 - D5"] * 2,
    "- D4 - F4 - Bb4 - F4 - D4 - F4 - Bb4 - D5", "- C#4 - E4 - A4 - E4 - C#4 - E4 - A4 - C#5")
PATROL_PERC = " ".join(["C2 C7 C8 C7 C5 C7 C8 C2 C2 C7 C8 C7 C5 C7 C8 C8"] * 8)

# Subterranean: slow, sparse; the lead answers the bells two ticks late from
# the other side - a ping-pong echo written into the score.
CAVE_BELLS = bars(
    "A5:4 -:4 D6:3 -:5", "F5:4 -:4 E5:3 -:5", "A5:4 -:4 C6:3 -:5", "G5:4 -:4 D5:3 -:5",
    "A5:4 -:4 D6:3 -:5", "F6:4 -:4 E6:3 -:5", "D6:4 -:4 A5:3 -:5", "E5:4 -:4 D5:3 -:5")
CAVE_ECHO = bars(
    "-:2 A4:4 -:4 D5:3 -:3", "-:2 F4:4 -:4 E4:3 -:3", "-:2 A4:4 -:4 C5:3 -:3", "-:2 G4:4 -:4 D4:3 -:3",
    "-:2 A4:4 -:4 D5:3 -:3", "-:2 F5:4 -:4 E5:3 -:3", "-:2 D5:4 -:4 A4:3 -:3", "-:2 E4:4 -:4 D4:3 -:3")
CAVE_BASS = bars("D2:8 D2:8", "D2:8 A1:8", "F2:8 F2:8", "G1:8 G1:8", "D2:8 D2:8", "Bb1:8 Bb1:8", "D2:8 A1:8", "A1:8 D2:8")
CAVE_PERC = " ".join(["-:6 C8:2 -:5 C8 -:2"] * 8)

JINGLE = {0: "C3:2 G3:2 C4:2 G3:2 C3:8", 1: "C5 E5 G5 C6:2 G5 C6:2 E6:8",
          2: "E4 G4 C5 E5:2 C5 E5:2 G5:8", 3: "C3 C7 C5 C7 C3 C7 C5 C5 C3:8"}

# Ending: the tarantella slows down and turns to A major.
ENDING_LEAD = bars("E5:3 E5:2 C#5", "B4:3 B4:2 D5", "C#5:3 C#5:2 A4", "G#4:3 B4:3",
                   "A4 C#5 E5 A5:3", "F#5:3 E5:2 D5", "E5 C#5 A4 B4 G#4 E4", "A4:3 A4:3")
ENDING_BASS = bars("A2:3 E3:3", "E2:3 B2:3", "A2:3 E3:3", "E2:3 E3:3", "A2:3 E3:3", "D3:3 A2:3", "E2:3 E3:3", "A2:3 A2:3")
ENDING_ARP = bars("- C#4 E4 - C#4 E4", "- B3 E4 - B3 E4", "- C#4 E4 - C#4 E4", "- B3 D4 - G#3 B3",
                  "- C#4 E4 - C#4 E4", "- D4 F#4 - D4 F#4", "- B3 E4 - G#3 D4", "- C#4 E4 - A3:2")

TRACKS = [
    ("title", track(112, {0: TARANTELLA_BASS, 1: TARANTELLA_LEAD, 2: TARANTELLA_ARP, 3: TARANTELLA_PERC})),
    ("surface", track(104, {0: PATROL_BASS, 1: PATROL_LEAD, 2: PATROL_ARP, 3: PATROL_PERC})),
    ("subterranean", track(62, {0: CAVE_BASS, 1: CAVE_ECHO, 2: CAVE_BELLS, 3: CAVE_PERC})),
    ("jingle", track(132, JINGLE, loop=False)),
    ("ending", track(72, {0: ENDING_BASS, 1: ENDING_LEAD, 2: ENDING_ARP})),
]


def main() -> int:
    config = {"samples": [], "instruments": INSTRUMENTS,
              "tracks": [dict(name=name, **t) for name, t in TRACKS]}
    out = ROOT / "audio/audio.json"
    out.write_text(json.dumps(config, indent=1) + "\n", encoding="utf-8")
    events = sum(len(t["events"]) for _n, t in TRACKS)
    print(f"wrote {out}: {len(INSTRUMENTS)} instruments, {len(TRACKS)} tracks, {events} events "
          f"({events * 4 + len(INSTRUMENTS) * 8 + len(TRACKS) * 8 + 40} bytes packed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
