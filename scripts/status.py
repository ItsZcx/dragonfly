#!/usr/bin/env python3
"""Render docs/img/status.svg from the phase list in docs/phases.md.

A phase counts as closed when the word "Closed" appears in its section of
docs/phases.md. That is the only place the status is written down, so the card
cannot disagree with the phase document.

    python3 scripts/status.py --write   # regenerate the card
    python3 scripts/status.py --check   # fail if the card is stale
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PHASES = ROOT / "docs" / "phases.md"
CARD = ROOT / "docs" / "img" / "status.svg"

HEADING = re.compile(r"^## P(\d+): (.+)$")
CLOSED = re.compile(r"\bClosed\b")

# The logo's palette.
NAVY = "#14172b"
EDGE = "#2a2f52"
BLUE = "#4c9be8"
TEAL = "#8ad8d2"
WHITE = "#f2f4ff"
MUTED = "#8b93b8"
DIM = "#5a6285"
TRACK = "#232a48"
ACTIVE = "#1d2a4d"


def read_phases() -> list[dict]:
    """Return one record per phase, in file order."""
    phases: list[dict] = []
    current: dict | None = None
    for line in PHASES.read_text(encoding="utf-8").splitlines():
        heading = HEADING.match(line)
        if heading:
            current = {"number": int(heading.group(1)), "title": heading.group(2), "closed": False}
            phases.append(current)
        elif current is not None and CLOSED.search(line):
            current["closed"] = True
    if not phases:
        sys.exit(f"{PHASES} lists no phases")
    numbers = [phase["number"] for phase in phases]
    if numbers != list(range(len(numbers))):
        sys.exit(f"{PHASES} skips a phase number: {numbers}")
    return phases


def sentence(phases: list[dict]) -> str:
    """Describe the card in words, for the alt text and the SVG description."""
    closed = [phase["number"] for phase in phases if phase["closed"]]
    total = len(phases)
    if not closed:
        return f"None of the {total} phases is closed."
    if len(closed) == total:
        return f"All {total} phases are closed."
    names = [f"P{number}" for number in closed]
    listed = names[0] if len(names) == 1 else f"{', '.join(names[:-1])}, and {names[-1]}"
    opened = [phase["number"] for phase in phases if not phase["closed"]]
    return f"{len(closed)} of {total} phases are closed: {listed}. P{opened[0]} is in progress."


def render(phases: list[dict]) -> str:
    total = len(phases)
    closed = sum(phase["closed"] for phase in phases)
    percent = round(100 * closed / total)

    width, height = 880, 148
    segments_x, segments_width = 248, 464
    slot = segments_width / total
    bar = 22
    bar_y, bar_h = 70, 16

    parts: list[str] = []
    for index, phase in enumerate(phases):
        x = segments_x + index * slot
        if phase["closed"]:
            fill, stroke = BLUE, "none"
        elif index == closed:
            fill, stroke = ACTIVE, TEAL
        else:
            fill, stroke = TRACK, "none"
        parts.append(
            f'<rect x="{x:.1f}" y="{bar_y}" width="{bar}" height="{bar_h}" rx="4" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="1.5"/>'
        )

    labels: list[str] = []
    for index, phase in enumerate(phases):
        x = segments_x + index * slot + bar / 2
        if phase["closed"]:
            fill = MUTED
        elif index == closed:
            fill = TEAL
        else:
            fill = DIM
        labels.append(
            f'<text x="{x:.1f}" y="108" font-size="10.5" fill="{fill}" '
            f'text-anchor="middle">P{phase["number"]}</text>'
        )

    font = "ui-sans-serif, -apple-system, 'Segoe UI', Helvetica, Arial, sans-serif"
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}"
     viewBox="0 0 {width} {height}" role="img" aria-labelledby="status-title status-desc">
  <title id="status-title">Dragonfly build status</title>
  <desc id="status-desc">{sentence(phases)}</desc>
  <rect x="0.5" y="0.5" width="{width - 1}" height="{height - 1}" rx="14" fill="{NAVY}" stroke="{EDGE}"/>
  <g font-family="{font}">
    <text x="32" y="50" font-size="12" letter-spacing="3" fill="{MUTED}">PHASES CLOSED</text>
    <text x="32" y="96" font-size="42" font-weight="700" fill="{WHITE}">{closed}
<tspan font-size="26" font-weight="400" fill="{MUTED}">/ {total}</tspan></text>
    <text x="848" y="50" font-size="12" letter-spacing="3" fill="{MUTED}" text-anchor="end">PROGRESS</text>
    <text x="848" y="96" font-size="42" font-weight="700" fill="{WHITE}" text-anchor="end">{percent}%</text>
    {"".join(parts)}
    {"".join(labels)}
  </g>
</svg>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--write", action="store_true", help="rewrite the card")
    group.add_argument("--check", action="store_true", help="fail if the card is stale")
    args = parser.parse_args()

    phases = read_phases()
    card = render(phases)
    print(sentence(phases))

    if args.write:
        CARD.parent.mkdir(parents=True, exist_ok=True)
        CARD.write_text(card, encoding="utf-8")
        print(f"wrote {CARD.relative_to(ROOT)}")
        return 0

    existing = CARD.read_text(encoding="utf-8") if CARD.exists() else ""
    if existing != card:
        print(f"{CARD.relative_to(ROOT)} is stale, run: python3 scripts/status.py --write", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
