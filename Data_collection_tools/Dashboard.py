import os
import csv
import socket
import threading
import customtkinter as ctk
from datetime import datetime

# --- 1. GLOBAL PATH SETUP ---
BASE_DIR = os.path.dirname(os.path.abspath(__file__))

# 2. Set the modern dark theme
ctk.set_appearance_mode("Dark")
ctk.set_default_color_theme("blue")

class RamDashboard(ctk.CTk):
    def __init__(self):
        super().__init__()

        # Window configuration
        self.title("Yankasa Ram Health - Data Collection Dashboard")
        self.geometry("1100x700")
        
        # --- State Variables ---
        self.current_behavior = "NONE"
        self.is_recording = False
        self.csv_filename = ""
        
        self.quotas = {
            "RESTING": 0,
            "GRAZING": 0,
            "AMBULATING": 0,
            "ANOMALY": 0
        }
        
        self.grid_columnconfigure(0, weight=1) 
        self.grid_columnconfigure(1, weight=1) 
        self.grid_columnconfigure(2, weight=1) 
        self.grid_rowconfigure(0, weight=1)

        # --- COLUMN 1: SENSOR FEED ---
        self.sensor_frame = ctk.CTkFrame(self)
        self.sensor_frame.grid(row=0, column=0, padx=10, pady=10, sticky="nsew")
        
        ctk.CTkLabel(self.sensor_frame, text="LIVE SENSOR STREAM", font=ctk.CTkFont(size=16, weight="bold")).pack(pady=10)
        
        # Extracted status label so we can update it from the network thread
        self.sensor_status_label = ctk.CTkLabel(self.sensor_frame, text="Status: WAITING FOR ESP32 ON PORT 8080...", text_color="orange")
        self.sensor_status_label.pack(pady=5)
        
        sensors = ["MPU-6050 (Movement)", "MAX30102 (Blood Ox)", "Piezo (Respiratory)", "DS18B20 (Animal Temp)", "DHT22 (Env Temp/Humid)"]
        for sensor in sensors:
            frame = ctk.CTkFrame(self.sensor_frame, fg_color="gray20")
            frame.pack(pady=5, padx=10, fill="x")
            ctk.CTkLabel(frame, text=sensor).pack(pady=10)

        # --- COLUMN 2: ACTIVITY LABELING ---
        self.label_frame = ctk.CTkFrame(self)
        self.label_frame.grid(row=0, column=1, padx=10, pady=10, sticky="nsew")

        ctk.CTkLabel(self.label_frame, text="CURRENT BEHAVIOR", font=ctk.CTkFont(size=16, weight="bold")).pack(pady=10)
        
        self.current_state_label = ctk.CTkLabel(self.label_frame, text="STATE: NONE", font=ctk.CTkFont(size=20, weight="bold"), text_color="gray")
        self.current_state_label.pack(pady=20)

        self.btn_rest = ctk.CTkButton(self.label_frame, text="R - RESTING", height=60, font=ctk.CTkFont(size=18), state="disabled", command=lambda: self.set_state("RESTING"))
        self.btn_rest.pack(pady=10, padx=20, fill="x")
        
        self.btn_graze = ctk.CTkButton(self.label_frame, text="G - GRAZING", height=60, font=ctk.CTkFont(size=18), state="disabled", command=lambda: self.set_state("GRAZING"))
        self.btn_graze.pack(pady=10, padx=20, fill="x")
        
        self.btn_ambulate = ctk.CTkButton(self.label_frame, text="A - AMBULATING", height=60, font=ctk.CTkFont(size=18), state="disabled", command=lambda: self.set_state("AMBULATING"))
        self.btn_ambulate.pack(pady=10, padx=20, fill="x")
        
        self.btn_anomaly = ctk.CTkButton(self.label_frame, text="X - ANOMALY", height=60, font=ctk.CTkFont(size=18), fg_color="#8B0000", hover_color="#5c0000", state="disabled", command=lambda: self.set_state("ANOMALY"))
        self.btn_anomaly.pack(pady=10, padx=20, fill="x")

        # --- KEYBINDINGS ---
        self.bind('<r>', lambda event: self.safe_keypress(event, "RESTING"))
        self.bind('<g>', lambda event: self.safe_keypress(event, "GRAZING"))
        self.bind('<a>', lambda event: self.safe_keypress(event, "AMBULATING"))
        self.bind('<x>', lambda event: self.safe_keypress(event, "ANOMALY"))

        # --- COLUMN 3: SESSION CONTROLS ---
        self.control_frame = ctk.CTkFrame(self)
        self.control_frame.grid(row=0, column=2, padx=10, pady=10, sticky="nsew")

        ctk.CTkLabel(self.control_frame, text="SESSION CONTROLS", font=ctk.CTkFont(size=16, weight="bold")).pack(pady=10)

        ctk.CTkLabel(self.control_frame, text="Ram ID (e.g., Ouda_01):").pack(pady=(10,0))
        self.ram_id_entry = ctk.CTkEntry(self.control_frame)
        self.ram_id_entry.pack(pady=5)

        self.btn_start = ctk.CTkButton(self.control_frame, text="START RECORDING", height=50, fg_color="green", hover_color="darkgreen", command=self.start_recording)
        self.btn_start.pack(pady=20, padx=20, fill="x")

        self.btn_stop = ctk.CTkButton(self.control_frame, text="STOP & SAVE", height=50, fg_color="red", hover_color="darkred", state="disabled", command=self.stop_recording)
        self.btn_stop.pack(pady=5, padx=20, fill="x")

        ctk.CTkLabel(self.control_frame, text="QUOTA TRACKER (Goal: 5 mins)", font=ctk.CTkFont(size=14, weight="bold")).pack(pady=(30, 10))
        
        self.quota_labels = {}
        for state in ["RESTING", "GRAZING", "AMBULATING", "ANOMALY"]:
            lbl = ctk.CTkLabel(self.control_frame, text=f"{state}: 0m 0s", font=ctk.CTkFont(size=14))
            lbl.pack(pady=2)
            self.quota_labels[state] = lbl

        # --- NETWORK SETUP ---
        self.UDP_IP = "0.0.0.0" # Listen on all network interfaces
        self.UDP_PORT = 8080
        
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.bind((self.UDP_IP, self.UDP_PORT))
            
            # Start the background listening thread
            self.network_thread = threading.Thread(target=self.udp_listener, daemon=True)
            self.network_thread.start()
        except Exception as e:
            self.sensor_status_label.configure(text=f"PORT ERROR: {e}", text_color="red")

        self.tick_clock()

    # --- NETWORK LISTENER THREAD ---
   # --- NETWORK LISTENER THREAD (UPDATED FOR TIMESTAMP) ---
    def udp_listener(self):
        """ This runs continuously in the background, independent of the UI """
        while True:
            try:
                # Wait for data from the ESP32 (1024 bytes buffer)
                data, addr = self.sock.recvfrom(1024)
                message = data.decode('utf-8').strip()
                
                # Safely update the UI status label using .after()
                self.after(0, lambda msg=message: self.sensor_status_label.configure(text=f"RECEIVING: [{msg[:25]}...]", text_color="#00FF00"))
                
                # If we are actively recording AND a label is selected, write to CSV
                if self.is_recording and self.current_behavior != "NONE":
                    with open(self.csv_filename, mode='a', newline='') as file:
                        writer = csv.writer(file)
                        
                        # 1. Split the incoming string into a list
                        row_data = message.split(',')
                        
                        # 2. Get the current exact time from the laptop
                        laptop_timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                        
                        # 3. Force the timestamp to the very front of the list (Index 0)
                        row_data.insert(0, laptop_timestamp)
                        
                        # 4. Append the behavior label to the very end
                        row_data.append(self.current_behavior)
                        
                        # 5. Write the perfectly formatted row to the CSV
                        writer.writerow(row_data)
                        
            except Exception as e:
                print(f"Network Error: {e}")

    # --- CORE FUNCTIONS ---
    def safe_keypress(self, event, state):
        if not self.is_recording:
            return 
        self.set_state(state)
        return "break"

    def set_state(self, new_state):
        self.current_behavior = new_state
        color = "red" if new_state == "ANOMALY" else "#00FF00" 
        self.current_state_label.configure(text=f"STATE: {new_state}", text_color=color)
        print(f"Behavior changed to: {new_state}")

    def tick_clock(self):
        if self.is_recording and self.current_behavior in self.quotas:
            self.quotas[self.current_behavior] += 1
            seconds_total = self.quotas[self.current_behavior]
            mins = seconds_total // 60
            secs = seconds_total % 60
            self.quota_labels[self.current_behavior].configure(text=f"{self.current_behavior}: {mins}m {secs}s")
        self.after(1000, self.tick_clock)

    def start_recording(self):
        ram_id = self.ram_id_entry.get().strip()
        if not ram_id:
            ram_id = "Unknown_Ram"
        
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        
        file_name = f"{ram_id}_{timestamp}.csv"
        self.csv_filename = os.path.join(BASE_DIR, file_name)
        
        with open(self.csv_filename, mode='w', newline='') as file:
            writer = csv.writer(file)
            writer.writerow(["Timestamp", "AccelX", "AccelY", "AccelZ", "GyroX", "GyroY", "GyroZ", "SpO2", "BPM", "PiezoVal", "AnimalTemp", "EnvTemp", "EnvHumid", "Behavior_Label"])
        
        self.is_recording = True
        self.focus() 
        self.ram_id_entry.configure(state="disabled", text_color="gray") 
        self.btn_start.configure(state="disabled", text="RECORDING ACTIVE...")
        self.btn_stop.configure(state="normal")
        self.btn_rest.configure(state="normal")
        self.btn_graze.configure(state="normal")
        self.btn_ambulate.configure(state="normal")
        self.btn_anomaly.configure(state="normal")
        
        print(f"Recording Started! Saving to: {self.csv_filename}")

    def stop_recording(self):
        self.is_recording = False
        self.ram_id_entry.configure(state="normal", text_color="white") 
        self.btn_start.configure(state="normal", text="START RECORDING")
        self.btn_stop.configure(state="disabled")
        self.btn_rest.configure(state="disabled")
        self.btn_graze.configure(state="disabled")
        self.btn_ambulate.configure(state="disabled")
        self.btn_anomaly.configure(state="disabled")
        self.current_behavior = "NONE"
        self.current_state_label.configure(text="STATE: NONE", text_color="gray")
        print("Recording Stopped!")

if __name__ == "__main__":
    app = RamDashboard()
    app.mainloop()