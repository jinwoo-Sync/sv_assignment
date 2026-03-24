#!/usr/bin/env python3
import json, os, sys, time

POLICY = os.path.join(os.path.dirname(__file__), "..", "configs", "policy.json")

def rw(data):
    with open(POLICY, "w") as f:
        json.dump(data, f)

def load():
    with open(POLICY) as f:
        orig = json.load(f)
    orig_safe = orig["safe"]
    orig["safe"] = 5.0
    rw(orig)
    print("fault injected")
    time.sleep(20)
    orig["safe"] = orig_safe
    rw(orig)
    print("ok")

if sys.argv[1] == "load":
    load()
