DVBridge 음성 팩 — 자연스러운 목소리 (Supertonic 3)
=====================================================

브릿지 공지의 "문장 읽어 주기"·예약 공지를 사람처럼 자연스러운 한국어 목소리(여성 5·남성 5)로 읽어 줍니다.
DVBridge 본체를 가볍게 두려고 따로 나눈 선택 설치 꾸러미입니다. 없어도 DVBridge는 Windows 기본 목소리로 동작합니다.


1. 설치
-------
- DVBridge를 먼저 설치한 뒤 DVBridge-voicepack-0.1-setup.exe 를 실행하세요. 설치된 DVBridge 폴더를 찾아
  그 안의 voice 폴더에 넣고, 켜져 있던 DVBridge는 다시 시작합니다. (포터블판은 폴더 선택에서 그 폴더를 고르세요)
- 설치 중 AI 음성 모델 라이선스(Open RAIL-M)에 동의해야 합니다. 따로 제거하지 않고 DVBridge를 제거할 때 함께 지워집니다.
- 웹 설정 → 브릿지 공지 → 목소리에서 "자연스러운 목소리(AI) · 여성 1" 등을 고르고,
  아래 "사용 제한" 칸에 동의한 뒤 [예약·목소리 저장]을 누릅니다. 동의하지 않으면 Windows 목소리로 읽습니다.
- 공지를 만들 때만 잠깐 실행되고 끝나면 메모리를 돌려줍니다. 한 문장에 몇 초 걸립니다.


2. AI 음성 모델 사용 제한 (꼭 읽어 주세요)
------------------------------------------
이 음성 팩의 음성 합성 모델 "Supertonic 3"는 Supertone Inc.가 BigScience Open RAIL-M 라이선스로 공개한 것입니다.
전문: voice\supertonic3\LICENSE   (모델 파일은 고치지 않았습니다. 출처: https://huggingface.co/Supertone/supertonic-3)

이 음성 팩을 쓰는 사람은 모델과 그 파생물을 다음 용도로 쓰거나 남이 쓰게 해서는 안 됩니다 (라이선스 부속서 A 요약).
  1) 국내외 법령·규정을 어기는 일
  2) 미성년자를 착취하거나 해치는 일, 또는 그 시도
  3) 남을 해칠 목적으로, 사실이 아님을 확인할 수 있는 거짓 정보·콘텐츠를 만들거나 퍼뜨리는 일
  4) 개인에게 해를 끼칠 수 있는 개인 식별 정보를 만들거나 퍼뜨리는 일
  5) 만든 정보·콘텐츠가 기계로 만든 것임을 분명히 알리지 않고 게시·송출하는 일
     → DVBridge는 이 음성 팩으로 만든 공지를 휴대폰 화면에 "브릿지 공지 (AI 음성)"으로 표시합니다
  6) 남의 명예를 훼손하거나 비방·괴롭히는 일
  7) 본인 동의 없이 다른 사람을 흉내 내거나 사칭하는 일
  8) 개인의 법적 권리에 불리한 영향을 주거나 구속력 있는 의무를 만들거나 바꾸는 완전 자동화된 결정
  9) 사회적 행동이나 개인 특성을 근거로 개인·집단을 차별하거나 해치는 일
 10) 나이·사회적·신체적·정신적 특성에 따른 취약성을 이용해 행동을 왜곡하고 피해를 주는 일
 11) 법으로 보호되는 특성·범주를 근거로 차별하는 일
 12) 의료 조언을 하거나 의료 결과를 해석하는 일
 13) 사법 행정·법 집행·출입국·난민 심사 절차에 쓸 정보를 만들거나 퍼뜨리는 일

이 음성 팩을 다른 사람에게 다시 나눠 줄 때는 이 사용 제한을 그 사람도 지키도록 함께 전하고,
voice\supertonic3\LICENSE 사본을 꼭 같이 주세요. 공지 내용에 대한 책임은 공지를 보내는 사람에게 있습니다.
Supertone Inc.의 공식 제품이 아니며, Supertone Inc.의 보증·제휴를 뜻하지 않습니다.


3. 들어 있는 것과 라이선스
--------------------------
- voice\dvbridge_tts.exe        : DVBridge 음성 팩 도우미 (MIT, Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR))
                                   추론 순서는 supertonic 파이썬 패키지(MIT, Copyright (c) 2025 Supertone Inc.)를 참고해 다시 작성
                                   → voice\supertonic-MIT-LICENSE.txt
- voice\onnxruntime.dll          : ONNX Runtime 1.31.1 (MIT, Copyright (c) Microsoft Corporation) 공식 배포판 그대로
                                   → voice\onnxruntime-LICENSE.txt, voice\onnxruntime-ThirdPartyNotices.txt
- voice\supertonic3\             : Supertonic 3 모델 (Supertone Inc., BigScience Open RAIL-M) → voice\supertonic3\LICENSE
