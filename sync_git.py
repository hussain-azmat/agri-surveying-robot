import os
import time
import requests

ROBOT_API_URL = "http://192.168.4.1/api/status"
LOG_FILE = "telemetry_logs.csv"

def push_to_github():
    try:
        os.system("git add .")
        os.system('git commit -m "Real-time telemetry sync"')
        os.system("git push origin main")
        print("Data successfully pushed to GitHub!")
    except Exception as e:
        print(f"Git push failed: {e}")

def main():
    print("Starting real-time 1-second wireless telemetry sync...")

    while True:
        try:
            response = requests.get(ROBOT_API_URL, timeout=2)
            if response.status_code == 200:
                data = response.json()
                log_line = f"[LOG 1s]: {data['millis']},{data['armed']},{data['heading']},{data['battery']},{data['lat']},{data['lon']},{data['satellites']}"
                print(log_line)
                
                # Append log to local file
                with open(LOG_FILE, "a") as f:
                    f.write(log_line + "\n")
                
                # Push to GitHub immediately every second
                push_to_github()
                
        except Exception as e:
            print(f"Waiting for robot Wi-Fi connection... ({e})")

        time.sleep(1)

if __name__ == "__main__":
    main()
