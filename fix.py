import os
import re

target_dir = r"c:\Users\Slaff\lumigrid-1\lumigrid-lander-main"

def fix_file(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception:
        return False
        
    new_content = content.replace("Lumigrid_Multi-channel PWM Driver", "Lumigrid_PCA9685")
    
    if new_content != content:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(new_content)
        print(f"Fixed: {filepath}")
        return True
    return False

def main():
    for root, dirs, files in os.walk(target_dir):
        if 'node_modules' in root or '.svelte-kit' in root:
            continue
        for file in files:
            if file.endswith(('.ts', '.svelte', '.md', '.json', '.html', '.mjs')):
                fix_file(os.path.join(root, file))

if __name__ == "__main__":
    main()
