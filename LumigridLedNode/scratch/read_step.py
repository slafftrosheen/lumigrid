import json

with open(r"C:\Users\alisi\.gemini\antigravity-ide\brain\a31ee12d-be50-4f69-a51f-d360b67ae124\.system_generated\logs\transcript.jsonl", "r", encoding="utf-8") as f:
    for line in f:
        data = json.loads(line)
        if data.get("step_index") in (363, 364, 365, 366):
            if "tool_calls" in data:
                print(f"Step {data.get('step_index')}:", data["tool_calls"])
            if "content" in data and "Task Description" in data.get("content"):
                print(f"Content {data.get('step_index')}:", data.get("content")[:200])
