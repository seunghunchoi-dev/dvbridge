// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "vocoder_host.h"

#include <algorithm>
#include <cstring>
#include <QDir>
#include <QLibrary>

namespace {
const uint8_t kDmrSilence[9] = {0xB9, 0xE8, 0x81, 0x52, 0x61, 0x73, 0x00, 0x2A, 0x6B};
}

VocoderHost::~VocoderHost() { unload(); }

void VocoderHost::unload()
{
	for (Slot &s : slots_) {
		if (s.plugin) {
			if (s.enc) s.plugin->close(s.enc);
			if (s.dec) s.plugin->close(s.dec);
		}
		s = Slot();
	}
	for (Plugin *p : plugins_) { p->lib->unload(); delete p->lib; delete p; }
	plugins_.clear();
}

bool VocoderHost::load(const QString &dir)
{
	unload();
	log_.clear();
	QStringList files = QDir(dir).entryList(QStringList() << "vocoder_*.dll", QDir::Files, QDir::Name);
	std::stable_partition(files.begin(), files.end(), [](const QString &f) { return !f.startsWith("vocoder_dummy", Qt::CaseInsensitive); });
	if (files.isEmpty()) log_ << QString("보코더 플러그인 없음 (%1)").arg(QDir::toNativeSeparators(dir));

	for (const QString &f : files) {
		QLibrary *lib = new QLibrary(QDir(dir).filePath(f));
		if (!lib->load()) { log_ << QString("%1: 로드 실패 - %2").arg(f, lib->errorString()); delete lib; continue; }
		auto ver = reinterpret_cast<int (*)()>(lib->resolve("vp_api_version"));
		auto name = reinterpret_cast<const char *(*)()>(lib->resolve("vp_name"));
		auto sup = reinterpret_cast<int (*)(int)>(lib->resolve("vp_supports"));
		Plugin *p = new Plugin;
		p->lib = lib; p->file = f;
		p->open = reinterpret_cast<vp_handle (*)(int)>(lib->resolve("vp_open"));
		p->close = reinterpret_cast<void (*)(vp_handle)>(lib->resolve("vp_close"));
		p->encode = reinterpret_cast<int (*)(vp_handle, const int16_t *, uint8_t *, int *)>(lib->resolve("vp_encode"));
		p->decode = reinterpret_cast<int (*)(vp_handle, const uint8_t *, int, int16_t *)>(lib->resolve("vp_decode"));
		if (!ver || !name || !sup || !p->open || !p->close || !p->encode || !p->decode || ver() != VP_API_VERSION) {
			log_ << QString("%1: 필수 함수 없음 또는 API 버전 불일치, 무시").arg(f);
			lib->unload(); delete lib; delete p; continue;
		}
		p->name = QString::fromUtf8(name());
		plugins_.append(p);
		QStringList used;
		for (int c = 1; c < kMaxCodec; ++c) {
			if (slots_[c].plugin || !sup(c)) continue;
			Slot s; s.plugin = p; s.enc = p->open(c); s.dec = p->open(c);
			if (!s.enc || !s.dec) { if (s.enc) p->close(s.enc); if (s.dec) p->close(s.dec); continue; }
			slots_[c] = s;
			used << QString::number(c);
		}
		log_ << QString("%1 (%2): 사용 코덱 [%3]").arg(f, p->name, used.join(','));
	}
	return has_codec(VP_CODEC_AMBE2_FEC);
}

bool VocoderHost::has_codec(int c) const { return c > 0 && c < kMaxCodec && slots_[c].plugin; }
QString VocoderHost::codec_plugin_name(int c) const { return has_codec(c) ? slots_[c].plugin->name : QString(); }

void VocoderHost::encode_dmr(const int16_t pcm[160], uint8_t frame[9])
{
	if (has_codec(VP_CODEC_AMBE2_FEC)) {
		int len = 9;
		Slot &s = slots_[VP_CODEC_AMBE2_FEC];
		if (s.plugin->encode(s.enc, pcm, frame, &len) == VP_OK && len == 9) return;
	}
	std::memcpy(frame, kDmrSilence, 9);
}

void VocoderHost::decode_dmr(const uint8_t frame[9], int16_t pcm[160])
{
	if (has_codec(VP_CODEC_AMBE2_FEC)) {
		Slot &s = slots_[VP_CODEC_AMBE2_FEC];
		if (s.plugin->decode(s.dec, frame, 9, pcm) == VP_OK) return;
	}
	std::memset(pcm, 0, 160 * sizeof(int16_t));
}
