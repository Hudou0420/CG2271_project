#!/usr/bin/env python3
"""Stand-in for the web dashboard: talks to the vault through Firebase.

Uses the same database paths as the real dashboard will (see web/README.md),
so it tests the ESP32's cloud link before the dashboard exists. Reads
FIREBASE_DB_URL and FIREBASE_API_KEY from Project_Secrets/secrets.h beside
the repository root. Standard library only.

    python3 dashboard_sim.py status              current /vault/status
    python3 dashboard_sim.py watch               follow status and new events
    python3 dashboard_sim.py arm 427 --player Bo send ARM, wait for the result
    python3 dashboard_sim.py disarm              send DISARM, wait for the result
    python3 dashboard_sim.py events [-n 20]      latest events
    python3 dashboard_sim.py leaderboard         fastest solves
"""
import argparse
import getpass
import json
import pathlib
import re
import ssl
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

SECRETS = pathlib.Path(__file__).resolve().parents[3] / "Project_Secrets" / "secrets.h"
ROOT = "/vault"


def load_config():
    try:
        text = SECRETS.read_text()
    except FileNotFoundError:
        sys.exit(f"{SECRETS} not found: place your configured secrets.h in Project_Secrets beside the repository root")

    def get(name):
        m = re.search(r'#define\s+%s\s+"([^"]*)"' % name, text)
        return m.group(1) if m else ""

    url = get("FIREBASE_DB_URL").rstrip("/")
    if not url.startswith("https://"):
        sys.exit("FIREBASE_DB_URL in secrets.h must start with https://")
    return url, get("FIREBASE_API_KEY")


def http_json(method, url, body=None, form=False):
    if body is None:
        data, ctype = None, "application/json"
    elif form:
        data, ctype = urllib.parse.urlencode(body).encode(), "application/x-www-form-urlencoded"
    else:
        data, ctype = json.dumps(body).encode(), "application/json"
    req = urllib.request.Request(url, data=data, method=method, headers={"Content-Type": ctype})
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:
                raw = resp.read()
            return json.loads(raw) if raw else None
        except urllib.error.HTTPError as e:
            if method == "GET" and e.code >= 500 and attempt < 2:
                time.sleep(attempt + 1)
                continue
            sys.exit(f"{method} {url.split('?')[0]}: HTTP {e.code}")
        except (urllib.error.URLError, ssl.SSLError, OSError) as e:
            if method == "GET" and attempt < 2:
                print("Connection interrupted; retrying read...", file=sys.stderr)
                time.sleep(attempt + 1)
                continue
            reason = e.reason if isinstance(e, urllib.error.URLError) else str(e)
            sys.exit(f"{method} {url.split('?')[0]}: {reason}")


class Db:
    def __init__(self, url, api_key, owner_email=None):
        self.url = url
        self.token = None
        if owner_email:
            if not api_key:
                sys.exit("FIREBASE_API_KEY is required for owner sign-in")
            password = getpass.getpass("Owner password (not stored): ")
            reply = http_json(
                "POST",
                "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + api_key,
                {"email": owner_email, "password": password, "returnSecureToken": True},
            )
            self.token = reply["idToken"]

    def call(self, method, path, body=None, **query):
        if self.token:
            query["auth"] = self.token
        url = f"{self.url}{ROOT}{path}.json"
        if query:
            url += "?" + urllib.parse.urlencode(query)
        return http_json(method, url, body)


def server_ms():
    return int(time.time() * 1000)


def show_status(st):
    if not st:
        print("no status yet: is the ESP32 online?")
        return
    age = (server_ms() - st.get("updatedAt", 0)) / 1000
    print(
        f"{st.get('state')}  mcuOnline={st.get('mcuOnline')}  dial={st.get('dial')} "
        f"digitsEntered={st.get('digitsEntered')}  knocks={st.get('knocks')} "
        f"attempts={st.get('attempts')}  closeness={st.get('closeness')} "
        f"light={st.get('light')}  (updated {age:.1f} s ago)"
    )


def show_event(key, ev):
    at = time.strftime("%H:%M:%S", time.localtime(ev.get("at", 0) / 1000))
    rest = {k: v for k, v in ev.items() if k not in ("type", "at")}
    print(f"  {at}  {ev.get('type'):<12} {json.dumps(rest) if rest else ''}")


def cmd_status(db, _args):
    show_status(db.call("GET", "/status"))


def cmd_events(db, args):
    evs = db.call("GET", "/events", orderBy='"$key"', limitToLast=args.n) or {}
    for key in sorted(evs):
        show_event(key, evs[key])
    if not evs:
        print("no events yet")


def cmd_leaderboard(db, _args):
    rows = list((db.call("GET", "/leaderboard") or {}).values())
    rows.sort(key=lambda r: (r.get("seconds", 1e9), r.get("attempts", 1e9)))
    for i, r in enumerate(rows[:10], 1):
        print(f"{i:2}. {r.get('player'):<{24}} {r.get('seconds')} s, {r.get('attempts')} attempts")
    if not rows:
        print("no solves yet")


def send_command(db, command):
    command["at"] = {".sv": "timestamp"}
    key = db.call("POST", "/commands", command)["name"]
    print(f"sent {command['action']} as /commands/{key}, waiting for the vault...")
    last = None
    for _ in range(20):  # the ESP32 polls once a second
        res = db.call("GET", f"/commandResults/{key}")
        if res and res.get("result") != last:
            last = res["result"]
            print(f"  result: {last}")
            if last != "sent":
                return
        time.sleep(0.5)
    print("  no final result after 10 s: is the ESP32 online? (try 'status')")


def cmd_arm(db, args):
    command = {"action": "arm", "code": args.code, "player": args.player}
    if args.pattern is not None:
        command["pattern"] = args.pattern
    send_command(db, command)


def cmd_disarm(db, _args):
    send_command(db, {"action": "disarm"})


def cmd_watch(db, _args):
    print("watching (Ctrl+C to stop)")
    last_status = None
    newest = db.call("GET", "/events", orderBy='"$key"', limitToLast=1) or {}
    last_key = max(newest) if newest else ""
    while True:
        st = db.call("GET", "/status")
        trimmed = {k: v for k, v in (st or {}).items() if k not in ("updatedAt", "uptimeS")}
        if trimmed != last_status:
            last_status = trimmed
            show_status(st)
        query = {"orderBy": '"$key"'}
        if last_key:
            query["startAt"] = json.dumps(last_key)
        evs = db.call("GET", "/events", **query) or {}
        for key in sorted(k for k in evs if k > last_key):
            show_event(key, evs[key])
            last_key = key
        time.sleep(1)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status").set_defaults(fn=cmd_status)
    sub.add_parser("watch").set_defaults(fn=cmd_watch)
    sub.add_parser("disarm").set_defaults(fn=cmd_disarm)
    sub.add_parser("leaderboard").set_defaults(fn=cmd_leaderboard)
    ev = sub.add_parser("events")
    ev.add_argument("-n", type=int, default=20)
    ev.set_defaults(fn=cmd_events)
    arm = sub.add_parser("arm")
    arm.add_argument("code", help="3 digits, e.g. 427")
    arm.add_argument("--player", default="anonymous")
    arm.add_argument("--pattern", type=int)
    arm.set_defaults(fn=cmd_arm)
    p.add_argument("--owner-email", help="shared owner account email (password is prompted, never stored)")
    args = p.parse_args()

    email = None
    if args.cmd in ("arm", "disarm"):
        email = args.owner_email or input("Owner email: ").strip()
        if not email:
            p.error("owner email is required for commands")
    try:
        db = Db(*load_config(), owner_email=email)
        args.fn(db, args)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
