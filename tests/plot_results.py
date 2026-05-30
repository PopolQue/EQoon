#!/usr/bin/env python3
"""5-panel visualization + summary dashboard for EQoon headless tests."""
import argparse
import json
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
from scipy.io import wavfile
from scipy import signal

# ── colour palette ──────────────────────────────────────────────────────
BG      = "#1a1a2e"
FG      = "#e0e0e0"
ACCENT  = "#00d4ff"
WARN    = "#ff6b6b"
GREEN   = "#51cf66"
GRID    = "#2a2a4a"
SPECTRUM_COLOUR = "#00d4ff"

plt.rcParams.update({
    "figure.facecolor": BG, "axes.facecolor": BG,
    "axes.edgecolor": FG, "axes.labelcolor": FG,
    "text.color": FG, "xtick.color": FG, "ytick.color": FG,
    "grid.color": GRID, "grid.alpha": 0.3,
})


def read_wav(path: str) -> tuple:
    sr, data = wavfile.read(path)
    if data.ndim == 1:
        data = data[:, None]
    if data.dtype == np.int16:
        data = data.astype(np.float64) / 32768.0
    elif data.dtype == np.int32:
        data = data.astype(np.float64) / 2147483648.0
    return sr, data


def magnitude_spectrum(samples: np.ndarray, sr: int, nperseg: int = 4096):
    f, t_spec, Zxx = signal.stft(samples[:, 0] if samples.ndim > 1 else samples,
                                  fs=sr, nperseg=nperseg, noverlap=nperseg // 2)
    mag = np.abs(Zxx)
    avg_mag = np.mean(mag, axis=1)
    avg_db = 20 * np.log10(np.maximum(avg_mag, 1e-10))
    return f, avg_db


def compute_freq_response(input_path: str, output_path: str, sr: int):
    """Compute frequency response H(f) = FFT(out) / FFT(in) via impulse."""
    sr_in, in_data = read_wav(input_path)
    sr_out, out_data = read_wav(output_path)
    assert sr_in == sr_out == sr

    N = min(len(in_data), len(out_data))
    n_fft = 1 << (int(np.log2(N)) - 1)
    in_ch = in_data[:n_fft, 0] if in_data.ndim > 1 else in_data[:n_fft]
    out_ch = out_data[:n_fft, 0] if out_data.ndim > 1 else out_data[:n_fft]

    win = np.hanning(n_fft)
    in_fft = np.fft.rfft(in_ch * win)
    out_fft = np.fft.rfft(out_ch * win)

    H = out_fft / np.maximum(in_fft, 1e-15)
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)
    mag_db = 20 * np.log10(np.maximum(np.abs(H), 1e-10))
    phase = np.unwrap(np.angle(H))
    return freqs, mag_db, phase, n_fft


def panel_waveform(ax, samples: np.ndarray, sr: int, title: str):
    ax.plot(np.arange(len(samples)) / sr, samples[:, 0] if samples.ndim > 1 else samples,
            color=ACCENT, lw=0.5)
    ax.set_title(title, color=FG, fontsize=9)
    ax.set_xlabel("Time (s)", fontsize=7)
    ax.set_ylabel("Amplitude", fontsize=7)
    ax.set_xlim(0, len(samples) / sr)
    ax.tick_params(labelsize=6)


def panel_magnitude_spectrum(ax, samples: np.ndarray, sr: int, title: str):
    f, mag_db = magnitude_spectrum(samples, sr)
    ax.semilogx(f[f > 0], mag_db[f > 0], color=ACCENT, lw=0.6)
    ax.set_title(title, color=FG, fontsize=9)
    ax.set_xlabel("Frequency (Hz)", fontsize=7)
    ax.set_ylabel("Magnitude (dB)", fontsize=7)
    ax.set_xlim(20, sr / 2)
    ax.set_ylim(-100, 10)
    ax.grid(True, alpha=0.2)
    ax.tick_params(labelsize=6)


def panel_freq_response(ax, freqs, mag_db, title: str):
    ax.semilogx(freqs[freqs > 0], mag_db[freqs > 0], color=SPECTRUM_COLOUR, lw=0.8)
    ax.axhline(0, color=GRID, ls="--", lw=0.5)
    ax.set_title(title, color=FG, fontsize=9)
    ax.set_xlabel("Frequency (Hz)", fontsize=7)
    ax.set_ylabel("Magnitude (dB)", fontsize=7)
    ax.set_xlim(20, 20000)
    ax.set_ylim(-30, 30)
    ax.grid(True, alpha=0.2)
    ax.tick_params(labelsize=6)


def panel_phase(ax, freqs, phase, title: str):
    ax.semilogx(freqs[freqs > 0], phase[freqs > 0], color=WARN, lw=0.8)
    ax.set_title(title, color=FG, fontsize=9)
    ax.set_xlabel("Frequency (Hz)", fontsize=7)
    ax.set_ylabel("Phase (rad)", fontsize=7)
    ax.set_xlim(20, 20000)
    ax.grid(True, alpha=0.2)
    ax.tick_params(labelsize=6)


def panel_spectrogram(ax, samples: np.ndarray, sr: int, title: str):
    ch = samples[:, 0] if samples.ndim > 1 else samples
    f, t_s, Sxx = signal.spectrogram(ch, fs=sr, nperseg=1024, noverlap=512)
    ax.pcolormesh(t_s, f, 10 * np.log10(Sxx + 1e-12),
                  shading="gouraud", cmap="inferno")
    ax.set_title(title, color=FG, fontsize=9)
    ax.set_xlabel("Time (s)", fontsize=7)
    ax.set_ylabel("Frequency (Hz)", fontsize=7)
    ax.set_ylim(0, sr // 2)
    ax.tick_params(labelsize=6)


def panel_goniometer(ax, samples: np.ndarray, title: str):
    if samples.ndim < 2 or samples.shape[1] < 2:
        ax.text(0.5, 0.5, "Mono", ha="center", va="center", color=FG, fontsize=9)
        return
    L = samples[:, 0]
    R = samples[:, 1]
    decay = 0.99
    n = min(48000, len(L))
    ax.scatter(L[:n], R[:n], s=0.3, c=ACCENT, alpha=0.5)
    ax.axhline(0, color=GRID, lw=0.3)
    ax.axvline(0, color=GRID, lw=0.3)
    ax.set_title(title, color=FG, fontsize=9)
    ax.set_xlabel("L", fontsize=7)
    ax.set_ylabel("R", fontsize=7)
    lim = max(np.max(np.abs(L[:n])), np.max(np.abs(R[:n])), 0.1)
    ax.set_xlim(-lim, lim)
    ax.set_ylim(-lim, lim)
    ax.set_aspect("equal")
    ax.tick_params(labelsize=6)


def panel_diff_spectrogram(ax, ref_samples, out_samples, sr: int, title: str):
    n = min(len(ref_samples), len(out_samples))
    ch = 0
    diff = (ref_samples[:n, ch] if ref_samples.ndim > 1 else ref_samples[:n]) - \
           (out_samples[:n, ch] if out_samples.ndim > 1 else out_samples[:n])
    f, t_s, Sxx = signal.spectrogram(diff, fs=sr, nperseg=1024, noverlap=512)
    ax.pcolormesh(t_s, f, 10 * np.log10(Sxx + 1e-12),
                  shading="gouraud", cmap="RdBu_r")
    ax.set_title(title, color=FG, fontsize=9)
    ax.set_xlabel("Time (s)", fontsize=7)
    ax.set_ylabel("Frequency (Hz)", fontsize=7)
    ax.set_ylim(0, sr // 2)
    ax.tick_params(labelsize=6)


def panel_overlaid_spectra(ax, spectra: list, title: str):
    for label, freqs, mag_db in spectra:
        ax.semilogx(freqs[freqs > 0], mag_db[freqs > 0], label=label, lw=0.8)
    ax.axhline(0, color=GRID, ls="--", lw=0.5)
    ax.set_title(title, color=FG, fontsize=9)
    ax.set_xlabel("Frequency (Hz)", fontsize=7)
    ax.set_ylabel("Magnitude (dB)", fontsize=7)
    ax.set_xlim(20, 20000)
    ax.set_ylim(-30, 30)
    ax.legend(fontsize=5, loc="best")
    ax.grid(True, alpha=0.2)
    ax.tick_params(labelsize=6)


def panel_rms_bar(ax, labels, values, title: str):
    colours = [GREEN if v < -3 else WARN for v in values]
    bars = ax.bar(labels, values, color=colours, width=0.6)
    ax.axhline(0, color=GRID, ls="--", lw=0.5)
    ax.set_title(title, color=FG, fontsize=9)
    ax.set_ylabel("RMS (dB)", fontsize=7)
    ax.tick_params(labelsize=6)
    for bar, val in zip(bars, values):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height(),
                f"{val:.1f}", ha="center", va="bottom" if val >= 0 else "top",
                fontsize=6, color=FG)


def panel_zoomed_spectrum(ax, freqs, mag_db, title: str, f_lo=100, f_hi=5000):
    mask = (freqs >= f_lo) & (freqs <= f_hi)
    ax.semilogx(freqs[mask], mag_db[mask], color=ACCENT, lw=0.8)
    ax.axhline(0, color=GRID, ls="--", lw=0.5)
    ax.set_title(title, color=FG, fontsize=9)
    ax.set_xlabel("Frequency (Hz)", fontsize=7)
    ax.set_ylabel("Magnitude (dB)", fontsize=7)
    ax.set_xlim(f_lo, f_hi)
    ax.grid(True, alpha=0.2)
    ax.tick_params(labelsize=6)


def make_5panel(input_path: str, output_path: str,
                scenario_name: str, output_dir: Path,
                panel5_fn=None):
    """Generate a 5-panel figure for a single scenario.

    panel5_fn(axes[4]) receives the 5th Axes for scenario-specific drawing.
    """
    sr_in, in_data = read_wav(input_path)
    sr_out, out_data = read_wav(output_path)
    sr = sr_in

    freqs, mag_db, phase, n_fft = compute_freq_response(input_path, output_path, sr)

    fig, axes = plt.subplots(2, 3, figsize=(14, 8))
    ax_wf = axes[0, 0]
    ax_mag = axes[0, 1]
    ax_fr = axes[0, 2]
    ax_ph = axes[1, 0]
    ax_p5 = axes[1, 1]
    # remove unused subplot
    axes[1, 2].axis("off")

    panel_waveform(ax_wf, out_data, sr, "Output Waveform")
    panel_magnitude_spectrum(ax_mag, out_data, sr, "Magnitude Spectrum")
    panel_freq_response(ax_fr, freqs, mag_db, "Frequency Response (dB)")
    panel_phase(ax_ph, freqs, phase, "Phase Response")

    if panel5_fn:
        panel5_fn(ax_p5)
    else:
        ax_p5.text(0.5, 0.5, "Scenario-specific\npanel not provided",
                   ha="center", va="center", color=FG, fontsize=9,
                   transform=ax_p5.transAxes)

    fig.suptitle(f"EQoon — {scenario_name}", color=FG, fontsize=12, y=0.98)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out_path = output_dir / f"{scenario_name.replace(' ', '_').lower()}.png"
    fig.savefig(out_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Wrote {out_path}")
    return out_path


def parse_stats_line(line: str) -> dict:
    """Parse a 'STATS key=val ...' line into a dict."""
    if not line.startswith("STATS"):
        return {}
    parts = line.strip().split()
    stats = {}
    for p in parts[1:]:
        if "=" in p:
            k, v = p.split("=", 1)
            try:
                stats[k] = float(v)
            except ValueError:
                stats[k] = v
    return stats


def build_summary_dashboard(results: list, output_dir: Path):
    """Create all_tests_summary.png with pass/fail + thumbnails."""
    n = len(results)
    cols = 4
    rows = (n + cols - 1) // cols

    fig, axes = plt.subplots(rows, cols, figsize=(cols * 4, rows * 4))
    axes = axes.flatten() if rows * cols > 1 else [axes]

    for i, r in enumerate(results):
        ax = axes[i]
        # Thumbnail
        img_path = r.get("plot_path", "")
        if img_path and Path(img_path).exists():
            img = plt.imread(str(img_path))
            ax.imshow(img)
        ax.set_title(f"{r['name']}\n{'PASS' if r['passed'] else 'FAIL'}",
                     color=GREEN if r['passed'] else WARN, fontsize=9)
        ax.axis("off")

    for j in range(i + 1, len(axes)):
        axes[j].axis("off")

    fig.suptitle("EQoon — All Tests Summary", color=FG, fontsize=14, y=0.98)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = output_dir / "all_tests_summary.png"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"Summary dashboard: {out}")


# ── CLI entry point ─────────────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="EQoon headless test visualizer")
    parser.add_argument("--scenario", required=True, help="Scenario name")
    parser.add_argument("--input", required=True, help="Input WAV path")
    parser.add_argument("--output", required=True, help="Output WAV path")
    parser.add_argument("--output-dir", default="tests/plots", help="Plot output dir")
    parser.add_argument("--panel5", choices=["spectrogram", "goniometer",
                                             "diff_spectrogram", "overlaid",
                                             "rms_bar", "zoomed_spectrum",
                                             "none"], default="none")
    parser.add_argument("--ref-wav", help="Reference WAV for diff_spectrogram")
    parser.add_argument("--summary-json", help="JSON file with all results for summary dashboard")
    args = parser.parse_args()

    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.summary_json:
        with open(args.summary_json) as f:
            results = json.load(f)
        build_summary_dashboard(results, out_dir)
        sys.exit(0)

    p5 = None
    sr_in, in_data = read_wav(args.input)
    sr_out, out_data = read_wav(args.output)

    if args.panel5 == "spectrogram":
        p5 = lambda ax: panel_spectrogram(ax, out_data, sr_out, "Spectrogram")
    elif args.panel5 == "goniometer":
        p5 = lambda ax: panel_goniometer(ax, out_data, "Goniometer")
    elif args.panel5 == "diff_spectrogram":
        if args.ref_wav:
            sr_ref, ref_data = read_wav(args.ref_wav)
            p5 = lambda ax: panel_diff_spectrogram(ax, ref_data, out_data, sr_out,
                                                    "Diff Spectrogram\n(ref – output)")
        else:
            p5 = lambda ax: ax.text(0.5, 0.5, "No ref_wav provided",
                                     ha="center", va="center", color=FG,
                                     transform=ax.transAxes)
    elif args.panel5 == "overlaid":
        spectra = [(args.input, *magnitude_spectrum(in_data, sr_in)[:2]),
                   (args.output, *magnitude_spectrum(out_data, sr_out)[:2])]
        p5 = lambda ax: panel_overlaid_spectra(ax, spectra, "Overlaid Spectra")
    elif args.panel5 == "rms_bar":
        rms_in = 20 * np.log10(np.maximum(np.sqrt(np.mean(in_data ** 2)), 1e-10))
        rms_out = 20 * np.log10(np.maximum(np.sqrt(np.mean(out_data ** 2)), 1e-10))
        p5 = lambda ax: panel_rms_bar(ax, ["Input", "Output"], [rms_in, rms_out],
                                       "RMS Level")
    elif args.panel5 == "zoomed_spectrum":
        _, mag_in = magnitude_spectrum(in_data, sr_in)
        freqs, mag_out = magnitude_spectrum(out_data, sr_out)
        p5 = lambda ax: panel_zoomed_spectrum(ax, freqs, mag_out,
                                               "Zoomed Spectrum (100–5kHz)")

    make_5panel(args.input, args.output, args.scenario, out_dir, panel5_fn=p5)
