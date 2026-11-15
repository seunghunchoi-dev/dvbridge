# SPDX-License-Identifier: MIT
# DVBridge 배포 패키지 만들기 (package.bat가 windeployqt 후 호출)
#   1) DVBridge-<ver>-win64.zip            본체 (포터블, 보코더 없음) - package.bat가 이것으로 설치 파일도 만듦
#   2) voicepack 폴더                        음성 팩 설치 파일(DVBridge-voicepack-<ver>-setup.exe)의 재료 (별도 내려받기용)
# 보코더는 배포하지 않음 (사용자가 직접 구함, 설치 파일에서 가진 파일을 고를 수 있음)
# 넣지 않는 것: 시험 프로그램, 보코더 파일, 설정·로그·이력·명단(개인정보), 개발 기록
import os, shutil, sys, zipfile

VER = sys.argv[4] if len(sys.argv) > 4 else '0.1'
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))	# dmrbridge (개발 폴더)
BUILD = sys.argv[1]			# C:\dev\DMR\build\dmrbridge
STAGE = sys.argv[2]			# windeployqt 결과 폴더 (DVBridge.exe + Qt)
OUT = sys.argv[3]			# 출력 폴더
MINGW_LIC = r'C:\Qt\Tools\mingw1310_64\licenses'
PK = os.path.join(ROOT, 'packaging')


def text(src, dst):
	# 메모장에서 깨지지 않도록 UTF-8(BOM) + CRLF
	s = open(src, encoding='utf-8', errors='replace').read().replace('\r\n', '\n')
	os.makedirs(os.path.dirname(dst), exist_ok=True)
	open(dst, 'w', encoding='utf-8-sig', newline='\r\n').write(s)


MANUAL = '사용 설명서.pdf'


def manual(dst_dir):
	# 설명서 PDF: packaging/manual/manual.html(+ img/)을 사진을 넣은 한 파일로 만든 뒤 Edge(화면 없이)로 인쇄
	#   한글 경로 문제를 피하려고 출력 폴더(영문 경로) 안 임시 폴더에서 만든 뒤 옮김. 실패하면 패키지 만들기도 실패
	import base64, re, subprocess, tempfile, pathlib, time
	src = os.path.join(PK, 'manual')
	html = open(os.path.join(src, 'manual.html'), encoding='utf-8').read()
	def inline(m):
		data = open(os.path.join(src, m.group(1)), 'rb').read()
		return 'src="data:image/png;base64,' + base64.b64encode(data).decode() + '"'
	html = re.sub(r'src="(img/[^"]+\.png)"', inline, html)
	edge = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), r'Microsoft\Edge\Application\msedge.exe')
	if not os.path.exists(edge): sys.exit('설명서 PDF를 만들 Microsoft Edge가 없습니다: ' + edge)
	tmp = tempfile.mkdtemp(prefix='dvb_manual_', dir=OUT)
	th, tp = os.path.join(tmp, 'manual.html'), os.path.join(tmp, 'manual.pdf')
	open(th, 'w', encoding='utf-8', newline='\n').write(html)
	subprocess.run([edge, '--headless=new', '--disable-gpu', '--no-pdf-header-footer', '--user-data-dir=' + os.path.join(tmp, 'prof'),
					'--print-to-pdf=' + tp, pathlib.Path(th).as_uri()], timeout=120)
	last = -1
	for _ in range(60):		# Edge 실행기가 먼저 끝나고 PDF는 조금 뒤에 써짐 → 크기가 멈출 때까지 기다림
		time.sleep(0.5)
		size = os.path.getsize(tp) if os.path.exists(tp) else -1
		if size > 0 and size == last: break
		last = size
	if not os.path.exists(tp):
		shutil.rmtree(tmp, ignore_errors=True)
		sys.exit('설명서 PDF를 만들지 못했습니다')
	out = os.path.join(dst_dir, MANUAL)
	shutil.move(tp, out)
	shutil.rmtree(tmp, ignore_errors=True)
	return out


def zipdir(folder, zpath, top):
	with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
		for r, _, fs in os.walk(folder):
			for f in sorted(fs):
				p = os.path.join(r, f)
				z.write(p, os.path.join(top, os.path.relpath(p, folder)) if top else os.path.relpath(p, folder))


# ---- 1) 본체 ----
name = 'DVBridge-%s-win64' % VER
app = os.path.join(OUT, name)
shutil.rmtree(app, ignore_errors=True)
shutil.copytree(STAGE, app)
for f in os.listdir(app):	# 보코더·시험 흔적이 섞이지 않게
	if f.lower().startswith('vocoder_') or f.lower().endswith('_test.exe'):
		os.remove(os.path.join(app, f))
text(os.path.join(ROOT, 'LICENSE'), os.path.join(app, 'LICENSE.txt'))
text(os.path.join(PK, 'README.txt'), os.path.join(app, 'README.txt'))
text(os.path.join(PK, 'addons_README.txt'), os.path.join(app, '추가 설치 안내.txt'))
text(os.path.join(PK, 'addons_README.txt'), os.path.join(OUT, '추가 설치 안내.txt'))	# 배포 파일 옆에도
print('manual:', manual(app))
text(os.path.join(PK, 'THIRD_PARTY_NOTICES.txt'), os.path.join(app, 'THIRD_PARTY_NOTICES.txt'))
text(os.path.join(MINGW_LIC, 'gcc', 'COPYING3'), os.path.join(app, 'LICENSES', 'GPL-3.0.txt'))
text(os.path.join(MINGW_LIC, 'gcc', 'COPYING3.LIB'), os.path.join(app, 'LICENSES', 'LGPL-3.0.txt'))
text(os.path.join(MINGW_LIC, 'gcc', 'COPYING.RUNTIME'), os.path.join(app, 'LICENSES', 'GCC-RUNTIME-LIBRARY-EXCEPTION.txt'))
text(os.path.join(MINGW_LIC, 'winpthreads', 'COPYING'), os.path.join(app, 'LICENSES', 'winpthreads.txt'))
plug = os.path.join(app, 'plugins')
shutil.rmtree(plug, ignore_errors=True)
text(os.path.join(PK, 'plugins_README.txt'), os.path.join(plug, 'README.txt'))
z1 = os.path.join(OUT, name + '.zip')
zipdir(app, z1, name)

zips = [z1]

# ---- 2) 음성 팩 (선택, 별도 내려받기): 자연스러운 목소리 Supertonic 3 → 설치 파일 재료 폴더 ----
#   모델 원본(수정 안 함)은 로컬 자료 DMR\work\voicepack\supertonic3 (Hugging Face Supertone/supertonic-3)
VP_MODEL = os.environ.get('DVB_VOICEPACK_MODEL', os.path.join(os.path.dirname(ROOT), 'work', 'voicepack', 'supertonic3'))
vp = os.path.join(OUT, 'DVBridge-voicepack-%s' % VER)
shutil.rmtree(vp, ignore_errors=True)
if os.path.isfile(os.path.join(VP_MODEL, 'onnx', 'vector_estimator.onnx')) and os.path.isfile(os.path.join(BUILD, 'dvbridge_tts.exe')):
	vd = os.path.join(vp, 'voice')
	os.makedirs(vd)
	TP = os.path.join(ROOT, 'third_party')
	shutil.copy2(os.path.join(BUILD, 'dvbridge_tts.exe'), vd)
	shutil.copy2(os.path.join(TP, 'onnxruntime', 'bin', 'onnxruntime.dll'), vd)
	text(os.path.join(TP, 'onnxruntime', 'LICENSE'), os.path.join(vd, 'onnxruntime-LICENSE.txt'))
	text(os.path.join(TP, 'onnxruntime', 'ThirdPartyNotices.txt'), os.path.join(vd, 'onnxruntime-ThirdPartyNotices.txt'))
	text(os.path.join(TP, 'supertonic-MIT-LICENSE.txt'), os.path.join(vd, 'supertonic-MIT-LICENSE.txt'))
	text(os.path.join(ROOT, 'LICENSE'), os.path.join(vd, 'dvbridge_tts-LICENSE.txt'))
	text(os.path.join(PK, 'voicepack_README.txt'), os.path.join(vd, 'README.txt'))
	md = os.path.join(vd, 'supertonic3')
	for sub in ('onnx', 'voice_styles'):
		shutil.copytree(os.path.join(VP_MODEL, sub), os.path.join(md, sub))
	for f in ('LICENSE', 'README.md'):
		shutil.copy2(os.path.join(VP_MODEL, f), md)
	text(os.path.join(VP_MODEL, 'LICENSE'), os.path.join(vp, 'MODEL_LICENSE.txt'))	# 설치 마법사 동의 화면용
	print('voicepack stage:', vp)
else:
	print('음성 팩: 모델(%s) 또는 dvbridge_tts.exe 없음 - 건너뜀' % VP_MODEL)

for z in zips:
	print('%s  %.1f MB' % (os.path.basename(z), os.path.getsize(z) / 1048576))
