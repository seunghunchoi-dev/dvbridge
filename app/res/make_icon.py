# SPDX-License-Identifier: MIT
# Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
# DMRBridge 아이콘 생성 (직접 그림 — 외부 그림·글꼴 사용 안 함)
#   사용: python -I make_icon.py <출력 폴더>  →  dmrbridge.ico (16~256), dmrbridge.png (256), favicon32.png
#   디자인: 빨강 그라데이션 둥근 사각형 + 흰 안테나 탑 + 양쪽 전파 (16·24px는 굵은 단순형)
import os, sys
from PIL import Image, ImageDraw

out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
S = 1024	# 크게 그린 뒤 줄여서 매끈하게


def lerp(a, b, t):
	return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def draw(detail):
	img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
	# 바탕: 위 밝은 빨강 → 아래 진한 빨강, 둥근 사각형
	bg = Image.new('RGBA', (S, S))
	top, bot = (236, 84, 64), (150, 30, 24)
	for y in range(S):
		ImageDraw.Draw(bg).line([(0, y), (S, y)], fill=lerp(top, bot, y / S) + (255,))
	mask = Image.new('L', (S, S), 0)
	ImageDraw.Draw(mask).rounded_rectangle([24, 24, S - 24, S - 24], radius=210, fill=255)
	img.paste(bg, (0, 0), mask)
	d = ImageDraw.Draw(img)
	W = (255, 255, 255, 255)
	cx = S // 2
	if detail:
		ty, r = 360, 58				# 꼭대기 원 중심·반지름
		base_y, half_top, half_base, lw = 820, 30, 170, 48
	else:							# 16·24px: 굵고 단순하게
		ty, r = 400, 92
		base_y, half_top, half_base, lw = 840, 40, 175, 92
	top_y = ty + r - 10
	# 안테나 탑 (위가 좁은 사다리꼴)
	d.line([(cx - half_top, top_y), (cx - half_base, base_y)], fill=W, width=lw)
	d.line([(cx + half_top, top_y), (cx + half_base, base_y)], fill=W, width=lw)
	if detail:						# 가로대 + 받침
		for t in (0.35, 0.68):
			y = top_y + (base_y - top_y) * t
			hw = half_top + (half_base - half_top) * t
			d.line([(cx - hw, y), (cx + hw, y)], fill=W, width=34)
		d.rounded_rectangle([cx - 270, base_y - 10, cx + 270, base_y + 46], radius=28, fill=W)
	d.ellipse([cx - r, ty - r, cx + r, ty + r], fill=W)
	# 전파 (양쪽 호) — 모서리 안쪽에 들어오게
	arcs = [(165, 42), (270, 42)] if detail else [(310, 92)]
	for rad, w in arcs:
		box = [cx - rad, ty - rad, cx + rad, ty + rad]
		d.arc(box, start=-42, end=42, fill=W, width=w)
		d.arc(box, start=138, end=222, fill=W, width=w)
	return img


big, small = draw(True), draw(False)
sizes = [16, 24, 32, 48, 64, 128, 256]
frames = [(small if s <= 24 else big).resize((s, s), Image.LANCZOS) for s in sizes]
frames[-1].save(os.path.join(out, 'dmrbridge.ico'), format='ICO', sizes=[(s, s) for s in sizes], append_images=frames[:-1])
frames[-1].save(os.path.join(out, 'dmrbridge.png'))
frames[2].save(os.path.join(out, 'favicon32.png'))
print('ok', out)
