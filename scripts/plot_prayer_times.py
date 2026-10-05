#!/usr/bin/env python3
"""
Generate interactive Plotly HTML plots of prayer times.

Layout assumed (matches docs/athantimes/ in this repo):

    <base>/<masjid>/<year>.json         e.g. davis/2027.json  (one positional file per year,
                                        prayertimes_specs.md)

For every <masjid>/<year>.json that exists, this writes:

    <base>/<masjid>/prayers_plots_<year>.html

Iqama times are ignored; only the 7 adhan/event times are plotted:
    fajr, sunrise, doha, dhuhar, asr, maghrib, isha

The yearly files are true 24-hour, so times are plotted as stored.

Usage:
    python plot_prayer_times.py <prayertimes_base_dir>

Example:
    python scripts/plot_prayer_times.py docs/athantimes
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
from pathlib import Path

try:
    import plotly.graph_objects as go
except ImportError:
    sys.exit("plotly is not installed. Install it with:  pip install plotly")


# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

# Order = legend order = drawing order (later traces drawn on top).
PRAYERS = ["fajr", "sunrise", "doha", "dhuhar", "asr", "maghrib", "isha"]

DISPLAY_NAMES = {
    "fajr":    "Fajr",
    "sunrise": "Sunrise",
    "doha":    "Doha",
    "dhuhar":  "Dhuhr",
    "asr":     "Asr",
    "maghrib": "Maghrib",
    "isha":    "Isha",
}

# Soft sunset/sunrise palette - walks from twilight -> dawn -> midday -> dusk -> night.
PRAYER_COLORS = {
    "fajr":    "#7c6daa",   # twilight lavender
    "sunrise": "#e9967a",   # soft dawn salmon
    "doha":    "#e8b14e",   # warm amber
    "dhuhar":  "#5fb3a1",   # midday seafoam
    "asr":     "#4a85ad",   # cool afternoon blue
    "maghrib": "#cf5c5c",   # sunset rose
    "isha":    "#2d3561",   # deep night blue
}

# Pretty masjid display labels (falls back to title-cased folder name).
MASJID_DISPLAY_NAMES = {
    "davis":        "Davis",
    "sclaraalnoor": "Santa Clara (Alnoor)",
    "sclaramca":    "Santa Clara (MCA)",
    "woodland":     "Woodland",
}


def display_masjid_name(key):
    return MASJID_DISPLAY_NAMES.get(key, key.replace("_", " ").title())


# Font stack used across the figure.
FONT_FAMILY = "Inter, -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif"

YEAR_RE = re.compile(r"^\d{4}$")
TIME_RE = re.compile(r"^([01][0-9]|2[0-3]):[0-5][0-9]$")


# ---------------------------------------------------------------------------
# Time helpers
# ---------------------------------------------------------------------------

def parse_time_to_minutes(timestr, prayer=None):
    """Convert "HH:MM" (24-hour) -> minutes since midnight; None for malformed input (plotly leaves a gap)."""
    if not timestr:
        return None
    s = timestr.strip()
    if not TIME_RE.match(s):
        return None
    return int(s[:2]) * 60 + int(s[3:5])


def minutes_to_hhmm(minutes):
    if minutes is None:
        return "--:--"
    h, m = divmod(int(minutes), 60)
    return f"{h:02d}:{m:02d}"


def is_leap(year):
    return year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)


# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------

def collect_year_data(year_file, year):
    """Read one yearly file (positional days) and build per-prayer time series."""
    dates = []
    series = {p: [] for p in PRAYERS}
    hover_times = {p: [] for p in PRAYERS}
    try:
        doc = json.loads(year_file.read_text())
    except (json.JSONDecodeError, OSError) as e:
        print(f"  warning: could not read {year_file}: {e}", file=sys.stderr)
        return {"dates": dates, "series": series, "hover_times": hover_times}
    column = {name: i for i, name in enumerate(doc["fields"])}
    for doy, day in enumerate(doc["days"], start=1):
        dates.append(dt.date(year, 1, 1) + dt.timedelta(days=doy - 1))
        for p in PRAYERS:
            mins = parse_time_to_minutes(day[column[p]], p)
            series[p].append(mins)
            hover_times[p].append(minutes_to_hhmm(mins))
    return {"dates": dates, "series": series, "hover_times": hover_times}


# ---------------------------------------------------------------------------
# Figure construction
# ---------------------------------------------------------------------------

def build_figure(masjid, year, data):
    fig = go.Figure()

    for prayer in PRAYERS:
        fig.add_trace(
            go.Scatter(
                x=data["dates"],
                y=data["series"][prayer],
                customdata=data["hover_times"][prayer],
                mode="lines+markers",
                name=DISPLAY_NAMES[prayer],
                line=dict(color=PRAYER_COLORS[prayer], width=2.2, shape="linear"),
                marker=dict(size=4, symbol="circle", line=dict(width=0),
                            color=PRAYER_COLORS[prayer]),
                hovertemplate="%{customdata}<extra></extra>",
                connectgaps=False,
            )
        )

    # Y axis: minutes-from-midnight as numeric; show HH:MM tick labels.
    y_tickvals = list(range(0, 24 * 60 + 1, 60))
    y_ticktext = [minutes_to_hhmm(v) for v in y_tickvals]

    # Tighten initial Y range to where prayers actually live, with padding;
    # full 0..1440 grid is still visible when the user zooms out.
    all_mins = [m for series in data["series"].values()
                for m in series if m is not None]
    if all_mins:
        y_min = max(0, min(all_mins) - 60)
        y_max = min(24 * 60, max(all_mins) + 60)
    else:
        y_min, y_max = 0, 24 * 60

    # X axis: explicit ticks on the 1st of each month, labelled "1-Jan".
    x_tickvals = [dt.date(year, m, 1) for m in range(1, 13)]

    masjid_display = display_masjid_name(masjid)

    fig.update_layout(
        title=dict(
            text=(
                f"Prayer Times  ·  <b>{masjid_display}</b> "
                f" ·  {year}"
                f"<br>"
                f"<span style='font-size:12px;color:#6b7280'>"
                f"click a name to toggle · double-click to solo · drag to zoom"
                f"</span>"
            ),
            x=0.5, xanchor="center",
            font=dict(family=FONT_FAMILY, size=22, color="#1f2937"),
        ),
        font=dict(family=FONT_FAMILY, size=12, color="#374151"),
        xaxis=dict(
            title=dict(text="Date", font=dict(size=13, color="#4b5563")),
            type="date",
            tickmode="array",
            tickvals=x_tickvals,
            tickformat="%-d-%b",
            tickangle=-45,
            tickfont=dict(size=11, color="#4b5563"),
            showgrid=True,
            gridcolor="#eef0f3",
            gridwidth=1,
            zeroline=False,
            linecolor="#d1d5db",
            showspikes=True,
            spikemode="across",
            spikesnap="cursor",
            spikedash="dot",
            spikecolor="#9ca3af",
            spikethickness=1,
            rangeselector=dict(
                buttons=[
                    dict(count=1,  label="1m",  step="month", stepmode="backward"),
                    dict(count=3,  label="3m",  step="month", stepmode="backward"),
                    dict(count=6,  label="6m",  step="month", stepmode="backward"),
                    dict(step="all", label="Year"),
                ],
                x=0.0, y=1.08,
                bgcolor="#f3f4f6",
                bordercolor="#e5e7eb",
                borderwidth=1,
                activecolor="#dbeafe",
                font=dict(family=FONT_FAMILY, size=11, color="#374151"),
            ),
            rangeslider=dict(
                visible=True,
                thickness=0.05,
                bgcolor="#f3f4f6",
                bordercolor="#e5e7eb",
            ),
        ),
        yaxis=dict(
            title=dict(text="Time of day (24h)",
                       font=dict(size=13, color="#4b5563")),
            tickmode="array",
            tickvals=y_tickvals,
            ticktext=y_ticktext,
            tickfont=dict(size=11, color="#4b5563"),
            range=[y_min, y_max],
            showgrid=True,
            gridcolor="#eef0f3",
            gridwidth=1,
            zeroline=False,
            linecolor="#d1d5db",
            showspikes=True,
            spikemode="across",
            spikesnap="cursor",
            spikedash="dot",
            spikecolor="#9ca3af",
            spikethickness=1,
        ),
        hovermode="x unified",
        hoverlabel=dict(
            bgcolor="rgba(255,255,255,0.96)",
            bordercolor="#e5e7eb",
            font=dict(family=FONT_FAMILY, size=12, color="#1f2937"),
        ),
        legend=dict(
            title=dict(text="<b>Prayer</b>",
                       font=dict(family=FONT_FAMILY, size=12, color="#1f2937")),
            orientation="v",
            yanchor="top", y=1.0,
            xanchor="left", x=1.02,
            bgcolor="rgba(255,255,255,0.96)",
            bordercolor="#e5e7eb",
            borderwidth=1,
            font=dict(family=FONT_FAMILY, size=12, color="#374151"),
            itemsizing="constant",
        ),
        updatemenus=[
            dict(
                type="buttons",
                direction="right",
                pad=dict(r=4, t=4),
                showactive=False,
                x=1.0, xanchor="right",
                y=1.08, yanchor="bottom",
                bgcolor="#f3f4f6",
                bordercolor="#e5e7eb",
                borderwidth=1,
                font=dict(family=FONT_FAMILY, size=11, color="#374151"),
                buttons=[
                    dict(label="Show all",
                         method="restyle",
                         args=[{"visible": [True] * len(PRAYERS)}]),
                    dict(label="Hide all",
                         method="restyle",
                         args=[{"visible": ["legendonly"] * len(PRAYERS)}]),
                ],
            ),
        ],
        template="plotly_white",
        paper_bgcolor="#ffffff",
        plot_bgcolor="#fbfbfd",
        margin=dict(l=80, r=180, t=120, b=80),
        autosize=True,
    )
    return fig


# ---------------------------------------------------------------------------
# Landing page (docs/index.html)
# ---------------------------------------------------------------------------

INDEX_HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP Athan · Prayer Time Charts</title>
<style>
  :root {
    --fg: #1f2937;
    --fg-muted: #6b7280;
    --bg: #fbfbfd;
    --card: #ffffff;
    --border: #e5e7eb;
    --accent: #5fb3a1;
    --link: #4a85ad;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    padding: 56px 24px;
    background: var(--bg);
    color: var(--fg);
    font-family: Inter, -apple-system, BlinkMacSystemFont, "Segoe UI",
                 Roboto, sans-serif;
    line-height: 1.55;
    -webkit-font-smoothing: antialiased;
  }
  .container { max-width: 880px; margin: 0 auto; }
  header { margin-bottom: 28px; }
  h1 {
    font-size: 30px;
    font-weight: 700;
    margin: 0 0 8px;
    letter-spacing: -0.01em;
  }
  h1 .dot { color: var(--accent); margin: 0 6px; }
  .subtitle {
    color: var(--fg-muted);
    font-size: 15px;
    margin: 0;
    max-width: 640px;
  }
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
    gap: 16px;
    margin-top: 28px;
  }
  .card {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 20px 22px;
    transition: box-shadow .15s ease, transform .1s ease,
                border-color .15s ease;
  }
  .card:hover {
    box-shadow: 0 6px 18px rgba(31, 41, 55, 0.08);
    transform: translateY(-1px);
    border-color: #d1d5db;
  }
  .card h2 {
    font-size: 17px;
    font-weight: 600;
    margin: 0 0 12px;
    color: var(--fg);
    border-bottom: 1px solid var(--border);
    padding-bottom: 10px;
  }
  .card ul { list-style: none; padding: 0; margin: 0; }
  .card li { margin: 2px 0; }
  .card a {
    color: var(--link);
    text-decoration: none;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 7px 10px;
    margin: 0 -10px;
    border-radius: 6px;
    font-size: 15px;
    transition: background .12s ease;
  }
  .card a:hover { background: #eef2f6; }
  .card a .arrow {
    color: var(--fg-muted);
    font-size: 14px;
    transition: transform .12s ease, color .12s ease;
  }
  .card a:hover .arrow {
    transform: translateX(2px);
    color: var(--link);
  }
  footer {
    margin-top: 48px;
    padding-top: 20px;
    border-top: 1px solid var(--border);
    text-align: center;
    color: var(--fg-muted);
    font-size: 13px;
  }
  footer a { color: var(--link); text-decoration: none; }
  footer a:hover { text-decoration: underline; }
  code {
    font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas,
                 "Liberation Mono", monospace;
    font-size: 12.5px;
    background: #f3f4f6;
    padding: 1px 6px;
    border-radius: 4px;
  }
</style>
</head>
<body>
  <div class="container">
    <header>
      <h1>ESP Athan<span class="dot">·</span>Prayer Time Charts</h1>
      <p class="subtitle">
        Interactive yearly plots of daily prayer times for each supported
        masjid. Click and drag to zoom, click a name in the legend to hide
        or solo a prayer.
      </p>
    </header>

    <div class="grid">
__CARDS__
    </div>

    <footer>
      Source: <a href="https://github.com/et7ad/esp_athan">github.com/et7ad/esp_athan</a>
      &nbsp;·&nbsp; generated by <code>scripts/plot_prayer_times.py</code>
    </footer>
  </div>
</body>
</html>
"""


def write_landing_index(out_dir, plots, base_dir_name):
    """Write a small landing page at out_dir/index.html linking to every plot.

    `plots` is a list of (masjid_key, year) tuples; `base_dir_name` is the
    folder name that holds masjid subdirs (e.g. "athantimes"), so links are
    "<base_dir_name>/<masjid>/prayers_plots_<year>.html" -- relative to the
    index, which keeps them working both on disk and on GitHub Pages.
    """
    from collections import defaultdict
    by_masjid = defaultdict(list)
    for masjid, year in plots:
        by_masjid[masjid].append(year)

    cards = []
    for masjid in sorted(by_masjid.keys()):
        display = display_masjid_name(masjid)
        years = sorted(by_masjid[masjid])
        items = []
        for year in years:
            link = f"{base_dir_name}/{masjid}/prayers_plots_{year}.html"
            items.append(
                f'          <li><a href="{link}">'
                f'<span>{year}</span><span class="arrow">&rarr;</span>'
                f'</a></li>'
            )
        cards.append(
            '      <div class="card">\n'
            f'        <h2>{display}</h2>\n'
            '        <ul>\n'
            + "\n".join(items) + "\n"
            '        </ul>\n'
            '      </div>'
        )

    html = INDEX_HTML_TEMPLATE.replace("__CARDS__", "\n".join(cards))
    out_path = out_dir / "index.html"
    out_path.write_text(html, encoding="utf-8")
    return out_path


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------

def process_base(base):
    if not base.is_dir():
        print(f"error: not a directory: {base}", file=sys.stderr)
        return 1

    written = 0
    plots = []  # collected (masjid, year) for the landing index
    masjid_dirs = sorted(p for p in base.iterdir() if p.is_dir())
    if not masjid_dirs:
        print(f"error: no subdirectories under {base}", file=sys.stderr)
        return 1

    for masjid_dir in masjid_dirs:
        year_files = sorted(p for p in masjid_dir.iterdir()
                            if p.is_file() and p.suffix == ".json" and YEAR_RE.match(p.stem))
        if not year_files:
            print(f"skipping {masjid_dir.name}: no <year>.json files found")
            continue

        for year_file in year_files:
            year = int(year_file.stem)
            print(f"processing {masjid_dir.name}/{year} ...")
            data = collect_year_data(year_file, year)
            if not data["dates"]:
                print(f"  no days in {year_file.name}, skipping")
                continue
            fig = build_figure(masjid_dir.name, year, data)
            out_path = masjid_dir / f"prayers_plots_{year}.html"
            fig.write_html(
                str(out_path),
                include_plotlyjs="cdn",
                full_html=True,
                config={
                    "responsive": True,
                    "displaylogo": False,
                    "scrollZoom": True,
                    "toImageButtonOptions": {
                        "filename": f"prayers_{masjid_dir.name}_{year}",
                        "format": "png",
                        "scale": 2,
                    },
                },
            )
            print(f"  wrote {out_path}  ({len(data['dates'])} days)")
            plots.append((masjid_dir.name, year))
            written += 1

    if written == 0:
        print("no <masjid>/<year>.json files produced output.",
              file=sys.stderr)
        return 1

    # Landing page lives one level above the base dir, so when base is
    # docs/athantimes, this writes docs/index.html -- which is the root
    # served by GitHub Pages when Pages source is the /docs folder.
    index_path = write_landing_index(base.parent, plots, base.name)
    print(f"wrote landing page {index_path}")

    print(f"done. wrote {written} plot file(s) + 1 index.")
    return 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("base",
                    help="path to the prayer-times base directory "
                         "(e.g. docs/athantimes)")
    args = ap.parse_args()
    sys.exit(process_base(Path(args.base).expanduser().resolve()))


if __name__ == "__main__":
    main()
