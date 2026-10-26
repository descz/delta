import sys, json
for line in sys.stdin:
    line=line.strip()
    if not line: continue
    try: msg=json.loads(line)
    except: continue
    mid=msg.get("id")
    if mid is None: continue
    if msg.get("method")=="initialize":
        print(json.dumps({"jsonrpc":"2.0","id":mid,"result":{"protocolVersion":"2024-11-05","capabilities":{}}}), flush=True)
    elif msg.get("method")=="tools/list":
        print(json.dumps({"jsonrpc":"2.0","id":mid,"result":{"tools":[{"name":"echo","description":"echo text"}]}}), flush=True)
    elif msg.get("method")=="tools/call":
        a=msg.get("params",{}).get("arguments",{})
        print(json.dumps({"jsonrpc":"2.0","id":mid,"result":{"content":[{"type":"text","text":"eco: "+str(a.get("x",""))} ]}}), flush=True)
    sys.stdout.flush()
