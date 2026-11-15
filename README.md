# DVBridge

BrandMeister 등 DMR 마스터(Homebrew 방식)에 접속해 휴대폰 앱과 음성을 이어 주는
**Windows용 아마추어 무선 브리지 프로그램**입니다. 가상 머신이나 WSL 없이 Windows에서 바로 동작합니다.

- 여러 사용자 동시 접속 (사용자마다 핫스팟 ID·USRP 포트)
- 웹 설정·대시보드 (송수신 상태·이력·실시간 듣기·서버 부하), 관리자 비밀번호
- RadioID 명단으로 콜사인·이름 표시
- 브릿지 공지: 문장 읽어 주기·마이크 방송·예약 공지를 이 브릿지의 휴대폰에만 (BM으로 나가지 않음)
- 내부 채널: BM으로 나가지 않는 이 브릿지 안의 채널 (기본 TG 99)
- 내부 시험: 내부 에코(9999#, BM 없이 내 목소리 되돌려 듣기), 웹 [🔊 시험] 소리 보내기, 사용자별 시험 모드(BM 접속 안 함)
- 지금 채널만 듣기: 지금 고른 채널과 다른 그룹 TG 소리는 휴대폰으로 보내지 않음, 채널을 바꾸면 예전 TG 자동 Unlink
- BrandMeister 루프 방지 규칙(60초 안 2초 미만 송신 6번이면 1시간 차단)에 걸리지 않도록 짧은 송신 제한
- 자동 실행·트레이·와치독(자동 복구), 업데이트 때 설정 유지

> ※ 아마추어 무선국 허가를 받은 분만 사용하세요.

## 내려받기

[**Releases**](../../releases)에서 받으세요.

| 파일 | 내용 |
|---|---|
| `DVBridge-0.1-setup.exe` | 설치 파일 (Windows 10·11 64비트) |
| `DVBridge-0.1-win64.zip` | 설치 없이 쓰는 포터블판 |
| `DVBridge-voicepack-0.1-setup.exe` | (선택) 자연스러운 목소리 음성 팩 — 브릿지 공지용 |
| `DVBridge-0.1-manual.pdf` | 사용 설명서 (인쇄용) |
| `DVBridge-0.1-addons-guide.txt` | 추가 설치 안내 — 따로 준비할 것 정리 |

**보코더(AMBE+2)는 들어 있지 않습니다.** DMR 음성을 쓰려면 보코더 플러그인(`vocoder_*.dll`)을 직접 구하시고,
그 라이선스·특허 조건도 직접 확인해 주세요. 설치 중 "보코더 플러그인 (선택)" 단계에서 고르거나 나중에 `plugins` 폴더에 넣습니다.

## 함께 쓰는 클라이언트 (PC판·안드로이드판)

DVBridge 서버에 붙어 DMR 무전을 듣고 말하는 전용 클라이언트도 함께 만들었습니다. 같은 MIT 라이선스입니다.
DVSwitch Mobile 대신 쓰거나 함께 쓸 수 있습니다.

| 클라이언트 | 저장소 | 내려받기 |
|---|---|---|
| **안드로이드판** | [dvbridge-client-android](https://github.com/seunghunchoi-dev/dvbridge-client-android) | [최신 릴리스](https://github.com/seunghunchoi-dev/dvbridge-client-android/releases/latest)의 APK |
| **PC판** (Windows) | [dvbridge-client-pc](https://github.com/seunghunchoi-dev/dvbridge-client-pc) | [최신 릴리스](https://github.com/seunghunchoi-dev/dvbridge-client-pc/releases/latest)의 설치 파일 |

클라이언트 사용법은 각 저장소의 Wiki와 설명서 PDF를 보세요.

## 사용 설명서

사진과 함께 따라 하는 설명서는 [**Wiki**](../../wiki)에 있습니다. 설치 폴더의 `사용 설명서.pdf`에도 같은 내용이 들어 있습니다.

## 소스에서 빌드하기

| 필요한 것 | 버전 |
|---|---|
| Qt (MinGW 64-bit 키트) | 6.11 이상 (6.11.3에서 시험) |
| MinGW-w64 GCC | 13.1 (Qt 설치 프로그램의 Tools) |
| CMake, Ninja | 3.21 이상 |
| Inno Setup | 6 (설치 파일을 만들 때만) |
| ONNX Runtime | 1.31.1 Windows x64 공식 배포판의 `onnxruntime.dll` → `third_party/onnxruntime/bin/` (음성 팩을 만들 때만) |

```bat
scripts\build.bat      :: 빌드 + 자동 시험
scripts\package.bat    :: 포터블 zip·설치 파일 (dist 폴더)
```

`scripts\*.bat`은 Qt 경로(`C:\Qt\6.11.3\mingw_64`)와 작업 경로(`C:\dev\DMR`)를 가정합니다. 환경에 맞게 고쳐 쓰세요.
경로에 한글이 있으면 Qt 도구가 실패할 수 있어 영문 경로(또는 junction)를 권합니다.

| 폴더 | 내용 |
|---|---|
| `core/dmr` | ETSI TS 102 361-1 기반 DMR 버스트·FEC·LC·음성 순서기 |
| `core/net` | Homebrew(MMDVM) 마스터 접속, RadioID 명단 받기 |
| `core/audio` | USRP (휴대폰 앱 연결) |
| `core/session` | 사용자별 브릿지 세션, 통화 이력, 공지·내부 채널 |
| `core/web` | 웹 설정·대시보드 서버 |
| `core/vocoder`, `plugin_api` | 보코더 플러그인 로더와 플러그인 규격 (보코더 자체는 없음) |
| `app` | Windows 프로그램 (간단 제어판·트레이·와치독·공지 음성 합성) |
| `tools/voicepack` | 음성 팩 도우미 (ONNX Runtime으로 Supertonic 3 실행) |
| `tests` | 자동 시험 (가짜 마스터 `tests/tools/fake_master.py`, PC 안에서만) |
| `packaging` | 설치 파일 스크립트, README, 설명서 원본 |

## 라이선스

**MIT License** — 누구나 자유롭게 사용·수정·재배포할 수 있습니다. 원저작자 표기를 유지해 주세요.

최초 제작: 최승훈 (6K2KZR)

함께 배포되는 다른 소프트웨어는 각자의 라이선스를 따릅니다.

- Qt 6 (LGPL v3, 동적 링크·수정 없음), MinGW-w64 런타임 (GCC Runtime Library Exception) — `packaging/THIRD_PARTY_NOTICES.txt`
- 음성 팩: ONNX Runtime (MIT, Microsoft), Supertonic 3 모델 (Supertone Inc., BigScience Open RAIL-M — 사용 제한 있음, `packaging/voicepack_README.txt`)

BrandMeister, RadioID는 각 소유자의 이름이며, 이 프로젝트는 그들과 제휴하지 않은 독립 프로젝트입니다.
