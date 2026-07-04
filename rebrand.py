import os
import re

# We will apply this to `lumigrid-lander-main` directory
target_dir = r"c:\Users\Slaff\lumigrid-1\lumigrid-lander-main"

replacements = [
    # API Renames
    (r"\$lib/api/wled", r"$lib/api/node"),
    (r"WledInfo", "NodeInfo"),
    (r"WledSegment", "NodeSegment"),
    (r"WledState", "NodeState"),
    (r"WledNode", "ApiNode"),
    
    # UI / Content Rebrands
    (r"WLED filesystem", "LED Node Storage"),
    (r"WLED FS", "Node Storage"),
    (r"WLED node", "LumiGrid LED Node"),
    (r"WLED HTTP API", "LumiGrid API"),
    (r"WLED", "LumiGrid LED Node"),
    
    # Hardware Rebrands
    (r"ESP32-WROOM-32U", "LumiGrid Node Core Processor"),
    (r"ESP32-S3", "LumiGrid Hub Processor"),
    (r"ESP32-D0WDQ5", "Node Processor"),
    (r"ESP32", "LED Node Board"),
    (r"PCA9685", "Multi-channel PWM Driver"),
    (r"SN74HCT245DWR", "Logic Level Shifter"),
    (r"DS18B20", "Digital Temperature Sensor"),
]

def process_file(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        return False
        
    new_content = content
    for pattern, repl in replacements:
        new_content = re.sub(pattern, repl, new_content, flags=re.IGNORECASE if not 'Wled' in pattern and not 'api/wled' in pattern else 0)
        
    if new_content != content:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(new_content)
        print(f"Updated: {filepath}")
        return True
    return False

def main():
    updated_count = 0
    for root, dirs, files in os.walk(target_dir):
        # Skip node_modules and .svelte-kit
        if 'node_modules' in root or '.svelte-kit' in root:
            continue
            
        for file in files:
            # only process text files
            if not file.endswith(('.ts', '.svelte', '.md', '.json', '.html', '.mjs')):
                continue
                
            filepath = os.path.join(root, file)
            if process_file(filepath):
                updated_count += 1
                
    print(f"Total files updated: {updated_count}")

    # Rename wled.ts to node.ts
    old_api = os.path.join(target_dir, 'src', 'lib', 'api', 'wled.ts')
    new_api = os.path.join(target_dir, 'src', 'lib', 'api', 'node.ts')
    if os.path.exists(old_api):
        os.rename(old_api, new_api)
        print(f"Renamed {old_api} to {new_api}")

if __name__ == "__main__":
    main()
