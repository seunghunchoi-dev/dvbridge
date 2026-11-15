// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// VocoderHost — plugins\vocoder_*.dll 로더. 세션마다 하나씩 만들어 씀(인스턴스 독립)
// 플러그인이 없거나 코덱을 지원하지 않으면 무음 PCM / 무음 프레임을 돌려주고 절대 멈추지 않음
#pragma once
#include <cstdint>
#include <QList>
#include <QString>
#include <QStringList>
#include "vocoder_plugin.h"

class QLibrary;

class VocoderHost
{
public:
	VocoderHost() = default;
	~VocoderHost();
	VocoderHost(const VocoderHost &) = delete;
	VocoderHost &operator=(const VocoderHost &) = delete;

	// dir 안의 vocoder_*.dll 로드. 같은 코덱을 여러 DLL이 지원하면 이름순, vocoder_dummy는 항상 마지막
	bool load(const QString &dir);
	bool has_codec(int codec) const;
	QString codec_plugin_name(int codec) const;
	QStringList log() const { return log_; }

	// DMR (AMBE+2 FEC 포함 9바이트)
	void encode_dmr(const int16_t pcm[160], uint8_t frame[9]);
	void decode_dmr(const uint8_t frame[9], int16_t pcm[160]);

private:
	struct Plugin {
		QLibrary *lib = nullptr;
		QString file, name;
		vp_handle (*open)(int) = nullptr;
		void (*close)(vp_handle) = nullptr;
		int (*encode)(vp_handle, const int16_t *, uint8_t *, int *) = nullptr;
		int (*decode)(vp_handle, const uint8_t *, int, int16_t *) = nullptr;
	};
	struct Slot { Plugin *plugin = nullptr; vp_handle enc = nullptr, dec = nullptr; };
	static constexpr int kMaxCodec = 8;
	void unload();

	QList<Plugin *> plugins_;
	Slot slots_[kMaxCodec];
	QStringList log_;
};
