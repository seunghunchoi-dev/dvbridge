# SPDX-License-Identifier: MIT
# Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#
# 시험용 가짜 Homebrew 마스터 (127.0.0.1 전용, 인터넷 사용 안 함)
#  - 로그인 비밀번호를 SHA256(salt+비밀번호)로 실제 검사, 틀리면 MSTNAK
#  - 핑에 MSTPONG, 받은 음성 스트림이 끝나면 1초 뒤 Parrot처럼 되돌려 보냄(src=9990, 새 스트림 ID)
#  - 기록: 로그인 시도 시각, RPTC 길이, RPTO 내용, RPTCL, 받은/보낸 DMRD 수
# 사용: python -I fake_master.py <초> <포트> <비밀번호> [로그파일]
import hashlib, os, socket, sys, threading, time

dur = float(sys.argv[1]); port = int(sys.argv[2]); password = sys.argv[3].encode()
logf = open(sys.argv[4], "w", encoding="utf-8") if len(sys.argv) > 4 else sys.stdout
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("127.0.0.1", port)); s.settimeout(0.3)
t0 = time.time(); end = t0 + dur
salt = os.urandom(4)
state = {}            # addr -> "login"|"auth"|"config"|"ok"
rec = {}            # (보낸 곳, 스트림 ID) -> 패킷 목록 (동시 스트림을 따로 모음)
stats = {"rptl": 0, "nak": 0, "rx_dmrd": 0, "echoed": 0, "rptcl": 0}

def log(*a):
    print(f"[{time.time()-t0:6.2f}]", *a, file=logf, flush=True)

def echo(frames, addr):
    time.sleep(1.0)
    for f in frames:
        b = bytearray(f); b[5:8] = (9990).to_bytes(3, "big"); b[16:20] = b"\x12\x34\x56\x78"
        s.sendto(bytes(b), addr); stats["echoed"] += 1; time.sleep(0.06)
    log("echoed", len(frames))

while time.time() < end:
    try:
        d, a = s.recvfrom(1024)
    except socket.timeout:
        continue
    if d.startswith(b"RPTL"):
        stats["rptl"] += 1; state[a] = "auth"; log("RPTL", d[4:8].hex())
        s.sendto(b"RPTACK" + salt, a)
    elif d.startswith(b"RPTK"):
        good = hashlib.sha256(salt + password).digest()
        if d[8:40] == good:
            state[a] = "config"; s.sendto(b"RPTACK" + d[4:8], a); log("RPTK ok")
        else:
            stats["nak"] += 1; s.sendto(b"MSTNAK" + d[4:8], a); log("RPTK wrong password -> MSTNAK")
    elif d.startswith(b"RPTCL"):
        stats["rptcl"] += 1; state.pop(a, None); log("RPTCL")
    elif d.startswith(b"RPTC"):
        log("RPTC len", len(d), "callsign", d[8:16], "description", d[78:97]); state[a] = "ok"; s.sendto(b"RPTACK" + d[4:8], a)
    elif d.startswith(b"RPTO"):
        log("RPTO", d[8:])
    elif d.startswith(b"RPTPING"):
        s.sendto(b"MSTPONG" + d[7:11], a)
    elif d.startswith(b"DMRD") and len(d) >= 53:
        stats["rx_dmrd"] += 1
        key = (a, d[16:20])
        if key not in rec:
            rec[key] = []
            log(f"stream start src={int.from_bytes(d[5:8],'big')} dst={int.from_bytes(d[8:11],'big')} flags={d[15]:02x}")
        rec[key].append(d)
        if (d[15] & 0x30) == 0x20 and (d[15] & 0x0F) == 2:
            frames = rec.pop(key)
            log("stream end, frames", len(frames)); threading.Thread(target=echo, args=(frames, a), daemon=True).start()
log("summary", stats)
