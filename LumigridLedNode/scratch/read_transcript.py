import json

with open(r'C:\Users\alisi\.gemini\antigravity-ide\brain\a31ee12d-be50-4f69-a51f-d360b67ae124\.system_generated\logs\transcript.jsonl', 'r', encoding='utf-8') as f:
    for line in f:
        data = json.loads(line)
        idx = data.get("step_index")
        if idx in (774, 775):
            print(f"=== Step {idx} ===")
            print(data.get("content", "").encode('ascii', errors='replace').decode('ascii'))
