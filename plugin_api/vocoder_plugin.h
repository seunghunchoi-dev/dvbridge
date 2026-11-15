/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
 *
 * vocoder_plugin.h — DVBridge 보코더 플러그인 C ABI (API 버전 2)
 *
 * - 본체는 이 헤더만 알고, AMBE 등 실제 코덱 구현은 별도 DLL에 둔다. (본체에는 코덱이 없음)
 * - DLL 이름 규칙: 실행파일 옆 plugins\vocoder_<name>.dll
 * - 모든 함수는 extern "C", 기본형만 사용 (컴파일러·버전이 달라도 동작)
 * - 오디오: 8000Hz, signed 16bit, mono, 20ms = 160샘플/프레임
 * - 코덱 프레임: 바이트로 묶음(MSB 우선). DMR은 FEC 포함 72비트 = 9바이트
 */
#ifndef VOCODER_PLUGIN_H
#define VOCODER_PLUGIN_H

#include <stdint.h>

#ifdef _WIN32
  #define VP_EXPORT __declspec(dllexport)
#else
  #define VP_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VP_API_VERSION 2

/* 코덱 ID */
enum {
    VP_CODEC_AMBE2_FEC  = 1, /* AMBE+2 3600x2450, FEC 포함 (DMR)     : 9 bytes */
    VP_CODEC_AMBE2_RAW  = 2, /* AMBE+2 2450, FEC 없음 (NXDN, YSF-DN) : 7 bytes */
    VP_CODEC_AMBE_DSTAR = 3, /* AMBE 3600x2400, FEC 포함 (D-Star)    : 9 bytes */
    VP_CODEC_IMBE_P25   = 4, /* IMBE (P25, YSF-VW)                   : 11 bytes */
    VP_CODEC_CODEC2     = 5  /* Codec2 3200 (M17)                    : 8 bytes */
};

/* 반환값 */
enum {
    VP_OK = 0,
    VP_ERR_UNSUPPORTED = -1,
    VP_ERR_BAD_ARG = -2,
    VP_ERR_INTERNAL = -3
};

typedef void* vp_handle;

VP_EXPORT int         vp_api_version(void);                 /* VP_API_VERSION 반환 */
VP_EXPORT const char* vp_name(void);                        /* 표시용 이름 (UTF-8) */
VP_EXPORT int         vp_supports(int codec_id);            /* 1=지원, 0=미지원 */
VP_EXPORT vp_handle   vp_open(int codec_id);                /* 인스턴스 (실패 시 NULL) */
VP_EXPORT void        vp_close(vp_handle h);
/* PCM 160샘플 → 코덱 프레임. out_len: 입력 = 버퍼 크기, 출력 = 실제 바이트 수 */
VP_EXPORT int vp_encode(vp_handle h, const int16_t* pcm160, uint8_t* out, int* out_len);
/* 코덱 프레임 → PCM 160샘플 */
VP_EXPORT int vp_decode(vp_handle h, const uint8_t* in, int in_len, int16_t* pcm160);

#ifdef __cplusplus
}
#endif

#endif /* VOCODER_PLUGIN_H */
