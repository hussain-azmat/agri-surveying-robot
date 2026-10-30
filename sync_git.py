import os
import time
import requests

ROBOT_API_URL = "http://192.168.4.1/api/status"
LOG_FILE_NAME = "robot_log.csv"

# Software ke andar jitne bhi function folders hain unki list
FOLDERS = [
    "Common", "GPS", "Logging", "MPU", "Motors", 
    "OTA", "Path", "Remote", "Rover", "Safety", "WiFi"
]

def main():
    print("Starting multi-folder telemetry logger under Software/...")
    print("Robot se data record ho kar sab folders mein save ho raha hai.")

    while True:
        try:
            response = requests.get(ROBOT_API_URL, timeout=2)
            if response.status_code == 200:
                data = response.json()
                log_line = f"[LOG 1s]: {data['millis']},{data['armed']},{data['heading']},{data['battery']},{data['lat']},{data['lon']},{data['satellites']}"
                print(log_line)
                
                # Sab folders ke andar robot_log.csv mein data save karein
                for folder in FOLDERS:
                    folder_path = os.path.join(".", folder)
                    if os.path.isdir(folder_path):
                        file_path = os.path.join(folder_path, LOG_FILE_NAME)
                        with open(file_path, "a") as f:
                            f.write(log_line + "\n")
                            
        except Exception as e:
            print(f"Waiting for robot Wi-Fi connection... ({e})")

        time.sleep(1)

if __name__ == "__main__":
    main()
