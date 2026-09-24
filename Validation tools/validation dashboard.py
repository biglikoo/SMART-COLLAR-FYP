import socket
import csv
from datetime import datetime
import customtkinter as ctk

# --- Configuration ---
UDP_IP = "0.0.0.0"
UDP_PORT = 5005
LOG_FILE = f"field_validation_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"

# Label Mapping
CLASS_MAP = {
    0: "Agitated",
    1: "Ambulating",
    2: "Feverish",
    3: "Grazing",
    4: "Respiratory Distress",
    5: "Resting",
    6: "Sluggish"
}

# Define which states are anomalies to color their buttons red
PATHOGENIC_STATES = [2, 4, 6]

class ModernValidationDashboard(ctk.CTk):
    def __init__(self):
        super().__init__()

        # --- Window Setup ---
        self.title("TinyML Edge Validation")
        self.geometry("900x550")
        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("blue")

        self.current_gt = "Resting"
        self.total_samples = 0
        self.correct_samples = 0

        # --- CSV Setup ---
        # Added 'Session_ID' to the columns
        self.csv_file = open(LOG_FILE, mode='w', newline='')
        self.csv_writer = csv.writer(self.csv_file)
        self.csv_writer.writerow([
            "Timestamp", "Session_ID", "Predicted_Index", "Predicted_Class", 
            "Ground_Truth", "Match", "Feature_Time_ms", 
            "Inference_Time_ms", "Total_Edge_ms", "Core_Temp_C"
        ])

        # --- Layout Configuration ---
        self.grid_columnconfigure(0, weight=1)
        self.grid_columnconfigure(1, weight=1)
        self.grid_rowconfigure(0, weight=1)

        self.build_gui()

        # --- UDP Socket Setup ---
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((UDP_IP, UDP_PORT))
        self.sock.setblocking(False)

        self.after(50, self.listen_udp)

    def build_gui(self):
        # ==========================================
        # LEFT PANEL: LIVE SENSOR & TIMING STREAM
        # ==========================================
        self.left_frame = ctk.CTkFrame(self, corner_radius=15)
        self.left_frame.grid(row=0, column=0, padx=20, pady=20, sticky="nsew")

        lbl_left_title = ctk.CTkLabel(self.left_frame, text="LIVE INFERENCE STREAM", font=ctk.CTkFont(size=16, weight="bold"))
        lbl_left_title.pack(pady=(20, 10))

        # --- NEW: SESSION ID INPUT ---
        self.session_frame = ctk.CTkFrame(self.left_frame, fg_color="transparent")
        self.session_frame.pack(pady=(0, 10))
        
        lbl_session = ctk.CTkLabel(self.session_frame, text="Session ID:", font=ctk.CTkFont(size=14))
        lbl_session.pack(side="left", padx=5)
        
        self.entry_session = ctk.CTkEntry(self.session_frame, placeholder_text="e.g., Yankasa_01", width=180)
        self.entry_session.pack(side="left", padx=5)
        # -----------------------------

        self.lbl_status = ctk.CTkLabel(self.left_frame, text="Status: WAITING FOR ESP32 ON PORT 5005...", text_color="#FFD700", font=ctk.CTkFont(size=12))
        self.lbl_status.pack(pady=(10, 20))

        # Prediction Display
        self.pred_frame = ctk.CTkFrame(self.left_frame, fg_color="#1f262a", corner_radius=10)
        self.pred_frame.pack(fill="x", padx=20, pady=10)
        self.lbl_pred = ctk.CTkLabel(self.pred_frame, text="STATE: NONE", font=ctk.CTkFont(size=24, weight="bold"))
        self.lbl_pred.pack(pady=15)

        # Timing & Vitals Display
        self.lbl_temp = ctk.CTkLabel(self.left_frame, text="Core Temp: -- °C", font=ctk.CTkFont(size=14))
        self.lbl_temp.pack(pady=5)
        
        self.lbl_infer_time = ctk.CTkLabel(self.left_frame, text="Inference Latency: -- ms", font=ctk.CTkFont(size=14))
        self.lbl_infer_time.pack(pady=5)

        self.lbl_feat_time = ctk.CTkLabel(self.left_frame, text="Feature Extraction: -- ms", font=ctk.CTkFont(size=14))
        self.lbl_feat_time.pack(pady=5)

        self.lbl_total_time = ctk.CTkLabel(self.left_frame, text="Total Edge Execution: -- ms", font=ctk.CTkFont(size=14, weight="bold"))
        self.lbl_total_time.pack(pady=5)

        self.lbl_accuracy = ctk.CTkLabel(self.left_frame, text="Live Field Match Rate: 0.0%", text_color="#00C853", font=ctk.CTkFont(size=16, weight="bold"))
        self.lbl_accuracy.pack(pady=(20, 10))


        # ==========================================
        # RIGHT PANEL: GROUND TRUTH CONTROLS
        # ==========================================
        self.right_frame = ctk.CTkFrame(self, corner_radius=15)
        self.right_frame.grid(row=0, column=1, padx=20, pady=20, sticky="nsew")

        lbl_right_title = ctk.CTkLabel(self.right_frame, text="CURRENT BEHAVIOR", font=ctk.CTkFont(size=16, weight="bold"))
        lbl_right_title.pack(pady=(20, 10))

        self.lbl_current_gt = ctk.CTkLabel(self.right_frame, text=f"Active Ground Truth:\n{self.current_gt.upper()}", font=ctk.CTkFont(size=18, weight="bold"), text_color="#A9B1D6")
        self.lbl_current_gt.pack(pady=(0, 20))

        # Generate Buttons dynamically
        for idx, name in CLASS_MAP.items():
            btn_color = "#C62828" if idx in PATHOGENIC_STATES else "#1f6aa5" # Red for anomalies, Blue for normal
            hover_color = "#b71c1c" if idx in PATHOGENIC_STATES else "#144870"
            
            btn = ctk.CTkButton(
                self.right_frame, 
                text=f"{idx} - {name.upper()}", 
                height=40,
                fg_color=btn_color,
                hover_color=hover_color,
                font=ctk.CTkFont(size=14, weight="bold"),
                command=lambda n=name: self.set_gt(n)
            )
            btn.pack(fill="x", padx=40, pady=8)
            self.bind(str(idx), lambda event, n=name: self.set_gt(n))

    def set_gt(self, label_name):
        self.current_gt = label_name
        self.lbl_current_gt.configure(text=f"Active Ground Truth:\n{self.current_gt.upper()}")

    def listen_udp(self):
        try:
            data, _ = self.sock.recvfrom(256)
            msg = data.decode('utf-8').strip()
            parts = msg.split(',')

            if len(parts) >= 5:
                pred_idx = int(parts[0])
                feat_ms = float(parts[1])
                infer_ms = float(parts[2])
                total_ms = float(parts[3])
                temp_c = float(parts[4])

                # Fetch Session ID dynamically from the input box
                session_id = self.entry_session.get().strip()
                if not session_id:
                    session_id = "Unnamed_Session"

                pred_class = CLASS_MAP.get(pred_idx, "Unknown")
                match = 1 if pred_class.lower() == self.current_gt.lower() else 0

                self.total_samples += 1
                self.correct_samples += match

                # Update UI
                self.lbl_status.configure(text="Status: RECEIVING LIVE DATA", text_color="#00C853")
                self.lbl_pred.configure(text=f"STATE: {pred_class.upper()}")
                
                if pred_idx in PATHOGENIC_STATES:
                    self.lbl_pred.configure(text_color="#FF5252")
                else:
                    self.lbl_pred.configure(text_color="#FFFFFF")

                self.lbl_temp.configure(text=f"Core Temp: {temp_c:.2f} °C")
                self.lbl_feat_time.configure(text=f"Feature Extraction: {feat_ms:.3f} ms")
                self.lbl_infer_time.configure(text=f"Inference Latency: {infer_ms:.3f} ms")
                self.lbl_total_time.configure(text=f"Total Edge Execution: {total_ms:.3f} ms")

                acc_rate = (self.correct_samples / self.total_samples) * 100
                self.lbl_accuracy.configure(text=f"Live Field Match Rate: {acc_rate:.1f}% ({self.correct_samples}/{self.total_samples})")

                # Log to CSV including the Session ID
                self.csv_writer.writerow([
                    datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                    session_id, pred_idx, pred_class, self.current_gt, match,
                    feat_ms, infer_ms, total_ms, temp_c
                ])
                self.csv_file.flush()

        except BlockingIOError:
            pass
        except Exception as e:
            print(f"Error parsing packet: {e}")

        self.after(50, self.listen_udp)

if __name__ == "__main__":
    app = ModernValidationDashboard()
    app.mainloop()