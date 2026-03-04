#!/usr/bin/env python3
"""
SSD1677 LUT Editor with Send-to-Device support.

Architecture:
- Each transition (B->B, B->W, W->B, W->W) has a voltage pattern that cycles.
- ALL transitions share the same timing groups (TP/RP).
- During Group N Phase A for X frames, each transition applies its own voltage.

Serial frame sent to device:
  [0xDE 0xAD 0xBE 0xEF]  magic (4 bytes)
  [length: uint32 LE]     payload length (4 bytes)
  [payload]               112-byte LUT
  [crc32: uint32 LE]      CRC-32/JAMCRC of payload (4 bytes)

The device logs the received bytes for verification; it does NOT yet apply the LUT.

Dependencies:
  pip install pyserial
"""

import binascii
import json
import struct
import threading
import tkinter as tk
from tkinter import messagebox, filedialog, scrolledtext, ttk
from typing import Dict, List, Tuple

try:
    import serial
    import serial.tools.list_ports

    SERIAL_AVAILABLE = True
except ImportError:
    SERIAL_AVAILABLE = False

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

VS_MAP = {0b00: "VSS", 0b01: "VSH1", 0b10: "VSL", 0b11: "VSH2"}
VS_REVERSE = {v: k for k, v in VS_MAP.items()}

TRANSITION_NAMES = ["Black → Black", "Black → White", "White → Black", "White → White"]
VOLTAGE_OPTIONS = ["VSS", "VSH1", "VSL", "VSH2"]

FRAME_MAGIC = bytes([0xDE, 0xAD, 0xBE, 0xEF])
LUT_SIZE = 112


# ---------------------------------------------------------------------------
# LUT encoding helpers
# ---------------------------------------------------------------------------


def encode_lut(
    voltage_patterns: Dict[int, List[str]],
    timing_groups: List[List[int]],
    frame_rate: int,
    voltages: Dict[str, int],
) -> bytes:
    """Encode editor state into the 112-byte SSD1677 LUT."""
    lut = bytearray(LUT_SIZE)

    # VS blocks L0–L3 (10 bytes each, 4 steps per byte, 2 bits per step)
    for trans_idx in range(4):
        pattern = voltage_patterns[trans_idx]
        base = trans_idx * 10
        byte_idx = step_idx = 0
        while step_idx < len(pattern) and byte_idx < 10:
            byte_val = 0
            for phase in range(4):
                if step_idx < len(pattern):
                    shift = 6 - phase * 2
                    byte_val |= VS_REVERSE.get(pattern[step_idx], 0) << shift
                    step_idx += 1
            lut[base + byte_idx] = byte_val
            byte_idx += 1
    # L4 (VCOM) – stays 0x00

    # TP/RP groups: 10 groups × 5 bytes starting at offset 50
    for g in range(10):
        base = 50 + g * 5
        for i in range(5):
            lut[base + i] = timing_groups[g][i]

    # Frame rate (5 bytes, all the same)
    for i in range(5):
        lut[100 + i] = frame_rate

    # Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    lut[105] = voltages["VGH"]
    lut[106] = voltages["VSH1"]
    lut[107] = voltages["VSH2"]
    lut[108] = voltages["VSL"]
    lut[109] = voltages["VCOM"]

    # Bytes 110–111: reserved, stay 0x00
    return bytes(lut)


def build_frame(payload: bytes) -> bytes:
    """Wrap a payload in the serial transfer frame."""
    length = struct.pack("<I", len(payload))
    crc = struct.pack("<I", binascii.crc32(payload) & 0xFFFFFFFF)
    return FRAME_MAGIC + length + payload + crc


# ---------------------------------------------------------------------------
# Main editor class
# ---------------------------------------------------------------------------


class LUTEditor:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("SSD1677 LUT Editor")
        self.root.geometry("1600x950")

        # --- LUT state ---
        self.voltage_patterns: Dict[int, List[str]] = {
            0: ["VSS", "VSH1", "VSS", "VSH1"],  # B->B
            1: ["VSL", "VSL", "VSL", "VSS"],  # B->W
            2: ["VSH1", "VSS", "VSH1", "VSS"],  # W->B
            3: ["VSS", "VSL", "VSS", "VSL"],  # W->W
        }
        self.timing_groups: List[List[int]] = [
            [4, 4, 0, 0, 0],
            [2, 2, 0, 0, 0],
            *[[0, 0, 0, 0, 0]] * 8,
        ]
        self.frame_rate = 0x88
        self.voltages = {
            "VGH": 0x17,
            "VSH1": 0x41,
            "VSH2": 0xA8,
            "VSL": 0x32,
            "VCOM": 0x30,
        }

        # UI widget holders
        self.voltage_widgets: Dict[int, dict] = {}
        self.timing_widgets: List[List[tk.Widget]] = []
        self.voltage_entries: Dict[str, tk.Entry] = {}

        self._build_ui()
        self.update_preview()

    # ------------------------------------------------------------------
    # UI construction
    # ------------------------------------------------------------------

    def _build_ui(self):
        paned = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        paned.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        left = ttk.Frame(paned)
        right = ttk.Frame(paned)
        paned.add(left, weight=2)
        paned.add(right, weight=1)

        self._build_left(left)
        self._build_right(right)

    def _build_left(self, parent: ttk.Frame):
        canvas = tk.Canvas(parent)
        scrollbar = ttk.Scrollbar(parent, orient="vertical", command=canvas.yview)
        inner = ttk.Frame(canvas)
        inner.bind(
            "<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
        )
        canvas.create_window((0, 0), window=inner, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)

        # Title
        hdr = ttk.Frame(inner)
        hdr.pack(fill=tk.X, padx=10, pady=10)
        ttk.Label(hdr, text="SSD1677 LUT Editor", font=("", 14, "bold")).pack()
        ttk.Label(
            hdr,
            text="Voltage patterns are per-transition · Timing groups are global",
            font=("", 9, "italic"),
        ).pack()

        # Voltage patterns
        vf = ttk.LabelFrame(inner, text="Voltage Patterns (per transition)", padding=10)
        vf.pack(fill=tk.X, padx=10, pady=5)
        for i in range(4):
            self._build_voltage_row(vf, i)

        # Timing groups
        tf = ttk.LabelFrame(
            inner, text="Timing Groups (global – used by all transitions)", padding=10
        )
        tf.pack(fill=tk.X, padx=10, pady=5)
        for i in range(10):
            self._build_timing_row(tf, i)

        # Global settings
        sf = ttk.LabelFrame(inner, text="Global Settings", padding=10)
        sf.pack(fill=tk.X, padx=10, pady=10)
        self._build_settings(sf)

        canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

    # ---- voltage pattern row ----

    def _build_voltage_row(self, parent, trans_idx: int):
        row = ttk.Frame(parent)
        row.pack(fill=tk.X, pady=3)
        ttk.Label(row, text=f"{TRANSITION_NAMES[trans_idx]}:", width=18).pack(
            side=tk.LEFT
        )
        container = ttk.Frame(row)
        container.pack(side=tk.LEFT, fill=tk.X, expand=True)
        add_btn = ttk.Button(
            row, text="+ Add", width=6, command=lambda: self._add_voltage(trans_idx)
        )
        add_btn.pack(side=tk.LEFT, padx=2)
        self.voltage_widgets[trans_idx] = {"container": container, "add_btn": add_btn}
        self._reload_voltage_row(trans_idx)

    def _reload_voltage_row(self, trans_idx: int):
        container = self.voltage_widgets[trans_idx]["container"]
        for w in container.winfo_children():
            w.destroy()
        pattern = self.voltage_patterns[trans_idx]
        for idx, voltage in enumerate(pattern):
            cell = ttk.Frame(container)
            cell.pack(side=tk.LEFT, padx=2)
            combo = ttk.Combobox(
                cell, values=VOLTAGE_OPTIONS, width=7, state="readonly"
            )
            combo.set(voltage)
            combo.pack(side=tk.LEFT)
            combo.bind(
                "<<ComboboxSelected>>",
                lambda e, t=trans_idx, v=idx: self._on_voltage_change(t, v),
            )
            if len(pattern) > 1:
                ttk.Button(
                    cell,
                    text="×",
                    width=2,
                    command=lambda t=trans_idx, v=idx: self._remove_voltage(t, v),
                ).pack(side=tk.LEFT)
        add_btn = self.voltage_widgets[trans_idx]["add_btn"]
        add_btn.config(state="disabled" if len(pattern) >= 40 else "normal")

    def _add_voltage(self, trans_idx: int):
        if len(self.voltage_patterns[trans_idx]) < 40:
            self.voltage_patterns[trans_idx].append("VSS")
            self._reload_voltage_row(trans_idx)
            self.update_preview()

    def _remove_voltage(self, trans_idx: int, volt_idx: int):
        if len(self.voltage_patterns[trans_idx]) > 1:
            self.voltage_patterns[trans_idx].pop(volt_idx)
            self._reload_voltage_row(trans_idx)
            self.update_preview()

    def _on_voltage_change(self, trans_idx: int, volt_idx: int):
        container = self.voltage_widgets[trans_idx]["container"]
        cell = container.winfo_children()[volt_idx]
        self.voltage_patterns[trans_idx][volt_idx] = cell.winfo_children()[0].get()
        self.update_preview()

    # ---- timing group row ----

    def _build_timing_row(self, parent, group_idx: int):
        row = ttk.Frame(parent)
        row.pack(fill=tk.X, pady=2)
        ttk.Label(row, text=f"Group {group_idx}:", width=10).pack(side=tk.LEFT)
        entries = []
        for pi, name in enumerate(["A", "B", "C", "D"]):
            ttk.Label(row, text=f"{name}:").pack(side=tk.LEFT, padx=2)
            spin = ttk.Spinbox(row, from_=0, to=255, width=5)
            spin.set(str(self.timing_groups[group_idx][pi]))
            spin.pack(side=tk.LEFT, padx=2)
            for event in ("<KeyRelease>", "<<Increment>>", "<<Decrement>>"):
                spin.bind(event, lambda e, g=group_idx: self._on_timing_change(g))
            entries.append(spin)
        ttk.Label(row, text="RP:").pack(side=tk.LEFT, padx=5)
        rp = ttk.Spinbox(row, from_=0, to=255, width=5)
        rp.set(str(self.timing_groups[group_idx][4]))
        rp.pack(side=tk.LEFT, padx=2)
        for event in ("<KeyRelease>", "<<Increment>>", "<<Decrement>>"):
            rp.bind(event, lambda e, g=group_idx: self._on_timing_change(g))
        entries.append(rp)
        self.timing_widgets.append(entries)

    def _on_timing_change(self, group_idx: int):
        for i, spin in enumerate(self.timing_widgets[group_idx]):
            try:
                self.timing_groups[group_idx][i] = int(spin.get() or 0)
            except ValueError:
                pass
        self.update_preview()

    # ---- global settings ----

    def _build_settings(self, parent):
        # Frame rate
        fr = ttk.Frame(parent)
        fr.pack(fill=tk.X, pady=5)
        ttk.Label(fr, text="Frame Rate:", font=("", 9, "bold")).pack(side=tk.LEFT)
        self.frame_rate_spin = ttk.Spinbox(fr, from_=1, to=255, width=6)
        self.frame_rate_spin.set(str(self.frame_rate))
        self.frame_rate_spin.pack(side=tk.LEFT, padx=5)
        for event in ("<KeyRelease>", "<<Increment>>", "<<Decrement>>"):
            self.frame_rate_spin.bind(event, lambda e: self._on_framerate_change())
        ttk.Label(fr, text="(lower = slower/more stable)").pack(side=tk.LEFT)

        # Voltages
        vf = ttk.Frame(parent)
        vf.pack(fill=tk.X, pady=5)
        ttk.Label(vf, text="Voltages:", font=("", 9, "bold")).pack(anchor=tk.W)
        grid = ttk.Frame(vf)
        grid.pack(fill=tk.X, pady=2)
        for i, name in enumerate(["VGH", "VSH1", "VSH2", "VSL", "VCOM"]):
            ttk.Label(grid, text=f"{name}:").grid(
                row=i, column=0, sticky=tk.W, padx=2, pady=2
            )
            entry = ttk.Entry(grid, width=8)
            entry.insert(0, f"0x{self.voltages[name]:02X}")
            entry.grid(row=i, column=1, padx=2, pady=2)
            entry.bind("<KeyRelease>", lambda e: self._on_voltage_setting_change())
            self.voltage_entries[name] = entry

    def _on_framerate_change(self):
        try:
            self.frame_rate = int(self.frame_rate_spin.get() or 0x44)
        except ValueError:
            pass
        self.update_preview()

    def _on_voltage_setting_change(self):
        for name, entry in self.voltage_entries.items():
            try:
                text = entry.get().strip()
                self.voltages[name] = (
                    int(text, 16 if text.startswith(("0x", "0X")) else 10) & 0xFF
                )
            except ValueError:
                pass
        self.update_preview()

    # ------------------------------------------------------------------
    # Right panel
    # ------------------------------------------------------------------

    def _build_right(self, parent: ttk.Frame):
        # --- Action buttons ---
        btn_row = ttk.Frame(parent)
        btn_row.pack(fill=tk.X, padx=5, pady=5)
        ttk.Button(btn_row, text="Load", command=self._load).pack(side=tk.LEFT, padx=2)
        ttk.Button(btn_row, text="Save", command=self._save).pack(side=tk.LEFT, padx=2)
        ttk.Button(btn_row, text="Reset", command=self._reset).pack(
            side=tk.LEFT, padx=2
        )
        ttk.Button(btn_row, text="Copy", command=self._copy).pack(side=tk.LEFT, padx=2)

        # --- Timing info ---
        info = ttk.LabelFrame(parent, text="Timing Info", padding=5)
        info.pack(fill=tk.X, padx=5, pady=5)
        self.timing_label = ttk.Label(info, text="", font=("", 9))
        self.timing_label.pack()

        # --- C array preview ---
        preview = ttk.LabelFrame(parent, text="C Array Output", padding=5)
        preview.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.preview_text = scrolledtext.ScrolledText(
            preview, wrap=tk.WORD, font=("Consolas", 9), height=12
        )
        self.preview_text.pack(fill=tk.BOTH, expand=True)

        # --- Send to Device ---
        send_frame = ttk.LabelFrame(parent, text="Send to Device", padding=8)
        send_frame.pack(fill=tk.X, padx=5, pady=5)

        port_row = ttk.Frame(send_frame)
        port_row.pack(fill=tk.X, pady=3)
        ttk.Label(port_row, text="COM Port:").pack(side=tk.LEFT)
        self.port_var = tk.StringVar(value="COM4")
        self.port_combo = ttk.Combobox(port_row, textvariable=self.port_var, width=12)
        self.port_combo.pack(side=tk.LEFT, padx=5)
        ttk.Button(port_row, text="↻ Refresh", command=self._refresh_ports).pack(
            side=tk.LEFT
        )
        self._refresh_ports()

        baud_row = ttk.Frame(send_frame)
        baud_row.pack(fill=tk.X, pady=3)
        ttk.Label(baud_row, text="Baud rate:").pack(side=tk.LEFT)
        self.baud_var = tk.StringVar(value="115200")
        ttk.Combobox(
            baud_row,
            textvariable=self.baud_var,
            values=["115200", "230400", "460800", "921600"],
            width=10,
            state="readonly",
        ).pack(side=tk.LEFT, padx=5)

        if not SERIAL_AVAILABLE:
            ttk.Label(
                send_frame,
                text="⚠  pyserial not installed  (pip install pyserial)",
                foreground="red",
            ).pack()

        ttk.Button(
            send_frame, text="▶  Send LUT to Device", command=self._send_to_device
        ).pack(fill=tk.X, pady=(6, 2))

        # --- Device log ---
        log_header = ttk.Frame(parent)
        log_header.pack(fill=tk.X, padx=5)
        ttk.Label(log_header, text="Device Log", font=("", 9, "bold")).pack(
            side=tk.LEFT
        )
        ttk.Button(log_header, text="Clear", command=self._clear_log).pack(
            side=tk.RIGHT
        )

        self.log_text = scrolledtext.ScrolledText(
            parent, wrap=tk.WORD, font=("Consolas", 9), height=10, state=tk.DISABLED
        )
        self.log_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=(0, 5))
        self.log_text.tag_config("ok", foreground="#00aa00")
        self.log_text.tag_config("err", foreground="#cc0000")
        self.log_text.tag_config("warn", foreground="#cc7700")
        self.log_text.tag_config("info", foreground="#222222")

    # ------------------------------------------------------------------
    # Preview / encode
    # ------------------------------------------------------------------

    def _calc_timing(self) -> Tuple[int, int]:
        total_frames = sum(sum(g[:4]) * (g[4] + 1) for g in self.timing_groups)
        ms = max(10.0, 2500.0 / self.frame_rate if self.frame_rate else 50.0)
        return total_frames, int(total_frames * ms * 1.1)

    def update_preview(self):
        lut = encode_lut(
            self.voltage_patterns, self.timing_groups, self.frame_rate, self.voltages
        )
        frames, ms = self._calc_timing()
        self.timing_label.config(text=f"Total frames: {frames} | Est. time: ~{ms} ms")

        out = "const uint8_t lut_custom[] = {\n"
        out += "  // VS L0–L3 (voltage patterns per transition)\n"
        for t in range(4):
            out += f"  // {TRANSITION_NAMES[t]}: [{' → '.join(self.voltage_patterns[t])}]\n  "
            out += "".join(f"0x{lut[t * 10 + b]:02X}," for b in range(10)) + "\n"
        out += "  // L4 (VCOM)\n  "
        out += "".join(f"0x{lut[40 + i]:02X}," for i in range(10)) + "\n\n"
        out += "  // TP/RP groups\n"
        for g in range(10):
            base = 50 + g * 5
            t = self.timing_groups[g]
            out += "  " + "".join(f"0x{lut[base + i]:02X}," for i in range(5))
            out += f"  // G{g}: A={t[0]} B={t[1]} C={t[2]} D={t[3]} RP={t[4]}"
            total = sum(t[:4]) * (t[4] + 1)
            if total:
                out += f" ({total} frames)"
            out += "\n"
        out += "\n  // Frame rate\n  "
        out += "".join(f"0x{lut[100 + i]:02X}," for i in range(5)) + "\n"
        out += "\n  // Voltages (VGH, VSH1, VSH2, VSL, VCOM)\n  "
        out += "".join(f"0x{lut[105 + i]:02X}," for i in range(5)) + "\n"
        out += "\n  // Reserved\n  0x00, 0x00\n};\n"

        self.preview_text.delete("1.0", tk.END)
        self.preview_text.insert("1.0", out)

    # ------------------------------------------------------------------
    # Serial send
    # ------------------------------------------------------------------

    def _refresh_ports(self):
        if not SERIAL_AVAILABLE:
            return
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo["values"] = ports
        if self.port_var.get() not in ports and ports:
            self.port_var.set(ports[0])

    def _log(self, text: str, tag: str = "info"):
        """Append a line to the device log (thread-safe via after)."""

        def _append():
            self.log_text.config(state=tk.NORMAL)
            self.log_text.insert(tk.END, text + "\n", tag)
            self.log_text.see(tk.END)
            self.log_text.config(state=tk.DISABLED)

        self.root.after(0, _append)

    def _clear_log(self):
        self.log_text.config(state=tk.NORMAL)
        self.log_text.delete("1.0", tk.END)
        self.log_text.config(state=tk.DISABLED)

    def _send_to_device(self):
        if not SERIAL_AVAILABLE:
            messagebox.showerror(
                "Missing dependency",
                "pyserial is not installed.\nRun: pip install pyserial",
            )
            return
        port = self.port_var.get().strip()
        if not port:
            messagebox.showerror("No port", "Select a COM port first.")
            return
        lut = encode_lut(
            self.voltage_patterns, self.timing_groups, self.frame_rate, self.voltages
        )
        frame = build_frame(lut)
        baud = int(self.baud_var.get())
        threading.Thread(
            target=self._do_send, args=(port, baud, frame), daemon=True
        ).start()

    def _do_send(self, port: str, baud: int, frame: bytes):
        """Background thread: open port, write frame, read responses for 4 s."""
        self._log(f"─── Connecting to {port} @ {baud} ───", "info")
        try:
            ser = serial.Serial()
            ser.port = port
            ser.baudrate = baud
            ser.timeout = 0.2
            ser.dtr = False
            ser.rts = False
            ser.open()
            try:
                ser.reset_input_buffer()
                ser.write(frame)
                self._log(
                    f"→ Sent {len(frame)} bytes  "
                    f"(magic+len+{len(frame)-12}B payload+crc)",
                    "ok",
                )
                # Read device responses for 4 seconds
                import time

                deadline = time.time() + 4.0
                buf = b""
                while time.time() < deadline:
                    chunk = ser.read(256)
                    if chunk:
                        buf += chunk
                        # Flush complete lines to the log
                        while b"\n" in buf:
                            line, buf = buf.split(b"\n", 1)
                            text = line.decode("utf-8", errors="replace").rstrip("\r")
                            tag = (
                                "ok"
                                if "OK" in text
                                else (
                                    "err"
                                    if "ERR" in text or "mismatch" in text.lower()
                                    else "warn" if "WARN" in text else "info"
                                )
                            )
                            self._log(text, tag)
                if buf.strip():
                    self._log(buf.decode("utf-8", errors="replace").rstrip(), "info")
                self._log("─── Done ───", "info")
            finally:
                ser.close()
        except serial.SerialException as exc:
            self._log(f"✗ Serial error: {exc}", "err")
        except Exception as exc:
            self._log(f"✗ Unexpected error: {exc}", "err")

    # ------------------------------------------------------------------
    # File I/O
    # ------------------------------------------------------------------

    def _save(self):
        path = filedialog.asksaveasfilename(
            defaultextension=".json",
            filetypes=[("JSON", "*.json"), ("All files", "*.*")],
        )
        if path:
            with open(path, "w") as f:
                json.dump(
                    {
                        "voltage_patterns": self.voltage_patterns,
                        "timing_groups": self.timing_groups,
                        "frame_rate": self.frame_rate,
                        "voltages": self.voltages,
                    },
                    f,
                    indent=2,
                )
            messagebox.showinfo("Saved", f"Saved to {path}")

    def _load(self):
        path = filedialog.askopenfilename(
            filetypes=[("JSON", "*.json"), ("All files", "*.*")]
        )
        if not path:
            return
        try:
            with open(path) as f:
                data = json.load(f)
            self.voltage_patterns = {
                int(k): v for k, v in data["voltage_patterns"].items()
            }
            self.timing_groups = data["timing_groups"]
            self.frame_rate = data["frame_rate"]
            self.voltages = data["voltages"]

            for i in range(4):
                self._reload_voltage_row(i)
            for g in range(10):
                for t in range(5):
                    self.timing_widgets[g][t].set(str(self.timing_groups[g][t]))
            self.frame_rate_spin.set(str(self.frame_rate))
            for name in self.voltages:
                self.voltage_entries[name].delete(0, tk.END)
                self.voltage_entries[name].insert(0, f"0x{self.voltages[name]:02X}")
            self.update_preview()
            messagebox.showinfo("Loaded", f"Loaded from {path}")
        except Exception as exc:
            messagebox.showerror("Load failed", str(exc))

    def _reset(self):
        if not messagebox.askyesno("Reset", "Reset to default LUT?"):
            return
        self.voltage_patterns = {
            0: ["VSS", "VSH1", "VSS", "VSH1"],
            1: ["VSL", "VSL", "VSL", "VSS"],
            2: ["VSH1", "VSS", "VSH1", "VSS"],
            3: ["VSS", "VSL", "VSS", "VSL"],
        }
        self.timing_groups = [[4, 4, 0, 0, 0], [2, 2, 0, 0, 0], *[[0, 0, 0, 0, 0]] * 8]
        self.frame_rate = 0x88
        self.voltages = {
            "VGH": 0x17,
            "VSH1": 0x41,
            "VSH2": 0xA8,
            "VSL": 0x32,
            "VCOM": 0x30,
        }
        for i in range(4):
            self._reload_voltage_row(i)
        for g in range(10):
            for t in range(5):
                self.timing_widgets[g][t].set(str(self.timing_groups[g][t]))
        self.frame_rate_spin.set(str(self.frame_rate))
        for name in self.voltages:
            self.voltage_entries[name].delete(0, tk.END)
            self.voltage_entries[name].insert(0, f"0x{self.voltages[name]:02X}")
        self.update_preview()

    def _copy(self):
        self.root.clipboard_clear()
        self.root.clipboard_append(self.preview_text.get("1.0", tk.END))
        messagebox.showinfo("Copied", "C array copied to clipboard.")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    root = tk.Tk()
    LUTEditor(root)
    root.mainloop()
