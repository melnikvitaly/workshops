"""Controls UI for detect_dots: presets, prompt input and protocol commands.

Provides a `Controls` class with methods:
 - draw() -> BGR image for the controls window
 - mouse(event,x,y,flags,param)  -- to pass to cv2.setMouseCallback
 - handle_key(key) -> bool        -- feed keys; returns True if consumed

It sends `link.set_gains`, `link.nudge`, `link.telemetry`, `link.query` directly.
"""
from typing import Optional, Dict, Any, List, Tuple
import time
import cv2
import numpy as np

class Controls:
    def __init__(self, link, size=(420, 820)):
        self.link = link
        self.size = size
        self.telemetry_on = False
        self.prompt_state: Optional[Dict[str, Any]] = None
        self.last_message = ""
        # Preset gain combinations. (pan_kp, pan_ki, pan_kd), (tilt...)
        #
        # Weighted towards D, because that is the term worth sweeping on this
        # rig: the gimbal overshoots and rings, which D fixes and I does not.
        # I is deliberately rare - it is only interesting as the fix for the
        # steady-state offset the PD rows leave standing, and as the thing that
        # winds up in Runaway. Tilt runs slightly softer than pan throughout
        # because it works against gravity.
        #
        # Four groups, in grid order: one term at a time -> two baselines ->
        # the PD ladder (same P, rising D) -> PD at other P levels.
        self.PRESETS = [
            # --- one term at a time: what each does on its own ---
            {"name": "All Zero", "pan": (0.0, 0.0, 0.0), "tilt": (0.0, 0.0, 0.0)},
            {"name": "P-only", "pan": (50.0, 0.0, 0.0), "tilt": (45.0, 0.0, 0.0)},
            {"name": "I-only", "pan": (0.0, 6.0, 0.0), "tilt": (0.0, 6.0, 0.0)},
            {"name": "D-only", "pan": (0.0, 0.0, 6.0), "tilt": (0.0, 0.0, 5.0)},

            # --- baselines: the firmware default, and it plus D ---
            {"name": "Default (PI)", "pan": (40.0, 4.0, 0.0), "tilt": (35.0, 4.0, 0.0)},
            {"name": "Full PID", "pan": (40.0, 4.0, 6.0), "tilt": (35.0, 4.0, 5.0)},

            # --- PD ladder: Default's P, no I, D climbing. Run these in order
            # against Default (PI) to see D trade overshoot for a standing
            # offset, then Full PID to see I close that offset back up.
            {"name": "PD Light", "pan": (40.0, 0.0, 3.0), "tilt": (35.0, 0.0, 2.5)},
            {"name": "PD", "pan": (40.0, 0.0, 6.0), "tilt": (35.0, 0.0, 5.0)},
            {"name": "PD Heavy", "pan": (40.0, 0.0, 12.0), "tilt": (35.0, 0.0, 10.0)},
            {"name": "PD Sluggish", "pan": (25.0, 0.0, 10.0), "tilt": (22.0, 0.0, 8.0)},

            # --- the same PD shape at other P levels ---
            {"name": "Aggressive PD", "pan": (80.0, 0.0, 10.0), "tilt": (70.0, 0.0, 9.0)},
            {"name": "Soft PD", "pan": (10.0, 0.0, 2.0), "tilt": (8.0, 0.0, 1.6)},
            {"name": "Tiny PD", "pan": (1.0, 0.0, 0.3), "tilt": (1.0, 0.0, 0.25)},

            # Deliberately unstable: huge P with an integrator and nothing to
            # damp it. Keep it last - it is the "what does windup look like"
            # demo, not a tuning candidate.
            {"name": "Runaway", "pan": (500.0, 200.0, 0.0), "tilt": (500.0, 200.0, 0.0)},
        ]

    def _make_buttons(self) -> List[Tuple[str, Tuple[int, int, int, int]]]:
        w, h = self.size
        btns = [
            ("K: set gains", (10, 10, w - 20, 50)),
            ("N: nudge", (10, 70, w - 20, 110)),
            ((f"T: telemetry {'ON' if self.telemetry_on else 'OFF'}"), (10, 130, w - 20, 170)),
            ("Q: query", (10, 190, w - 20, 230)),
        ]
        # presets in two columns
        presets_y = 260
        col_width = (w - 30) // 2
        preset_height = 60
        preset_btns = []
        for i, p in enumerate(self.PRESETS):
            col = i % 2
            row = i // 2
            x1 = 10 + col * (col_width + 10)
            y = presets_y + row * preset_height
            preset_btns.append((p['name'], (x1, y, x1 + col_width, y + preset_height)))
        btns += preset_btns
        return btns

    def draw(self) -> np.ndarray:
        w, h = self.size
        img = np.zeros((h, w, 3), np.uint8)
        img[:] = (30, 30, 30)
        btns = self._make_buttons()
        for label, (x1, y1, x2, y2) in btns:
            cv2.rectangle(img, (x1, y1), (x2, y2), (80, 80, 80), -1)
            cv2.rectangle(img, (x1, y1), (x2, y2), (180, 180, 180), 1)

            preset = next((p for p in self.PRESETS if p['name'] == label), None)
            if preset is not None:
                name = preset['name']
                pan = ", ".join(f"{v:g}" for v in preset['pan'])
                tilt = ", ".join(f"{v:g}" for v in preset['tilt'])
                lines = [name, f"pan: ({pan})", f"tilt: ({tilt})"]
                line_gap = 14
                base_y = y1 + 18
                for i, line in enumerate(lines):
                    y = base_y + i * line_gap
                    font_scale = 0.36 if i == 0 else 0.28
                    cv2.putText(img, line, (x1 + 6, y), cv2.FONT_HERSHEY_SIMPLEX,
                                font_scale, (230, 230, 230), 1, cv2.LINE_AA)
            else:
                cv2.putText(img, label, (x1 + 6, y1 + 22), cv2.FONT_HERSHEY_SIMPLEX,
                            0.5, (230, 230, 230), 1, cv2.LINE_AA)
        # prompt area
        if self.prompt_state is not None:
            p = self.prompt_state
            if p['kind'] == 'K':
                fields = ["axis (p/t/b)", "KP", "KI", "KD"]
            else:
                fields = ["dpan (deg)", "dtilt (deg)"]
            oy = self.size[1] - (len(fields) * 22) - 20
            for i, f in enumerate(fields):
                status = p['inputs'][i] if i < len(p['inputs']) else (p['buffer'] if i == p['idx'] else "")
                cv2.putText(img, f"{f}: {status}", (10, oy + i * 22), cv2.FONT_HERSHEY_SIMPLEX,
                            0.55, (240, 240, 240), 1, cv2.LINE_AA)
        # last message
        if self.last_message:
            cv2.putText(img, self.last_message, (10, self.size[1] - 6), cv2.FONT_HERSHEY_SIMPLEX,
                        0.5, (200, 240, 200), 1, cv2.LINE_AA)
        return img

    def mouse(self, event, x, y, flags, param):
        if event != cv2.EVENT_LBUTTONDOWN and event != cv2.EVENT_RBUTTONDOWN:
            return
        btns = self._make_buttons()
        telemetry_label = f"T: telemetry {'ON' if self.telemetry_on else 'OFF'}"
        for label, (x1, y1, x2, y2) in btns:
            if x1 <= x <= x2 and y1 <= y <= y2:
                # Control actions must match exact labels; preset names like
                # "Tiny Gains" also start with T, so prefix checks are wrong.
                if label == "K: set gains" and event == cv2.EVENT_LBUTTONDOWN:
                    self.prompt_state = {"kind": "K", "inputs": [], "idx": 0, "buffer": ""}
                elif label == "N: nudge" and event == cv2.EVENT_LBUTTONDOWN:
                    self.prompt_state = {"kind": "N", "inputs": [], "idx": 0, "buffer": ""}
                elif label == telemetry_label and event == cv2.EVENT_LBUTTONDOWN:
                    self.telemetry_on = not self.telemetry_on
                    try:
                        self.link.telemetry(1 if self.telemetry_on else 0)
                        self.last_message = f"Telemetry {'ON' if self.telemetry_on else 'OFF'}"
                    except Exception as e:
                        self.last_message = f"telemetry failed: {e}"
                elif label == "Q: query" and event == cv2.EVENT_LBUTTONDOWN:
                    try:
                        self.link.query()
                        self.last_message = "Query sent"
                    except Exception as e:
                        self.last_message = f"query failed: {e}"
                else:
                    # preset buttons
                    preset = next((p for p in self.PRESETS if p['name'] == label), None)
                    if preset is not None and event == cv2.EVENT_LBUTTONDOWN:
                        try:
                            pan = preset['pan']
                            tilt = preset['tilt']
                            self.link.set_gains('p', pan[0], pan[1], pan[2])
                            self.link.set_gains('t', tilt[0], tilt[1], tilt[2])
                            self.last_message = f"Applied preset {label}"
                        except Exception as e:
                            self.last_message = f"apply preset failed: {e}"
                return

    def handle_key(self, key: int) -> bool:
        if self.prompt_state is None:
            return False
        p = self.prompt_state
        # Enter
        if key in (13, 10):
            p['inputs'].append(p['buffer'].strip())
            p['buffer'] = ""
            p['idx'] += 1
            # completion
            if (p['kind'] == 'K' and p['idx'] >= 4) or (p['kind'] == 'N' and p['idx'] >= 2):
                try:
                    if p['kind'] == 'K':
                        axis = p['inputs'][0]
                        kp = float(p['inputs'][1])
                        ki = float(p['inputs'][2])
                        kd = float(p['inputs'][3])
                        self.link.set_gains(axis, kp, ki, kd)
                        self.last_message = f"Gains set {axis} {kp},{ki},{kd}"
                    else:
                        dpan = float(p['inputs'][0])
                        dtilt = float(p['inputs'][1])
                        self.link.nudge(dpan, dtilt)
                        self.last_message = f"Nudge {dpan},{dtilt}"
                except Exception as e:
                    self.last_message = f"prompt command failed: {e}"
                self.prompt_state = None
            return True
        # Esc
        if key == 27:
            self.prompt_state = None
            return True
        # Backspace
        if key in (8, 127):
            p['buffer'] = p['buffer'][:-1]
            return True
        # printable
        if 32 <= key <= 126:
            p['buffer'] += chr(key)
            return True
        return False
