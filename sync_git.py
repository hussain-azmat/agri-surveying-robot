import time
import requests

ROBOT_API_URL = "http://192.168.4.1/api/status"
LOG_FILE = "telemetry_logs.csv"

def main():
    print("Starting per-second local telemetry logger...")
    print("Robot se data record ho raha hai. Terminal rokne ke liye Ctrl+C dabayein.")

    while True:
        try:
            response = requests.get(ROBOT_API_URL, timeout=2)
            if response.status_code == 200:
                data = response.json()
                log_line = f"[LOG 1s]: {data['millis']},{data['armed']},{data['heading']},{data['battery']},{data['lat']},{data['lon']},{data['satellites']}"
                print(log_line)
                
                # Per second local file mein save hota rahega
                with open(LOG_FILE, "a") as f:
                    f.write(log_line + "\n")
        except Exception as e:
            print(f"Waiting for robot Wi-Fi connection... ({e})")

        time.sleep(1)

if __name__ == "__main__":
    main()
