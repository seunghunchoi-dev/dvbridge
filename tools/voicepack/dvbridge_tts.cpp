// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
//
// dvbridge_tts — DVBridge 음성 팩용 문장 읽기 도우미 (Supertonic 3 모델, ONNX Runtime)
//   DVBridge가 공지를 만들 때만 잠깐 실행되고 끝나면 메모리를 모두 돌려줌 (본체는 가볍게 유지)
//   추론 순서(문장 정리 → 길이 예측 → 문장 인코더 → 흐름 정합 단계 → 보코더)는
//   supertonic 파이썬 패키지(MIT, Copyright (c) 2025 Supertone Inc.)의 공개 동작을 보고 C++로 다시 작성
//   모델 파일(Supertone Inc., BigScience Open RAIL-M)은 이 프로그램에 들어 있지 않고 음성 팩에 따로 있음
//
// 사용: dvbridge_tts --model <supertonic3 폴더> --voice F1 --in <문장 UTF-8 파일> --out <8kHz WAV>
//                     [--speed 1.05] [--steps 8] [--lang ko]
//       dvbridge_tts --model <폴더> --list      목소리 이름 목록 (VOICE|이름)
//   onnxruntime.dll 은 이 실행 파일과 같은 폴더에서 불러옴
#define _USE_MATH_DEFINES
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include "onnxruntime_c_api.h"

namespace {

std::wstring widen(const std::string &s)
{
	if (s.empty()) return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
	std::wstring w(size_t(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
	return w;
}

bool read_file(const std::wstring &path, std::string &out)
{
	std::ifstream f(path.c_str(), std::ios::binary);
	if (!f) return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

// ---- 아주 작은 JSON 도우미 (숫자 배열·정수 값만 필요) ----
// key 다음의 대괄호 덩어리 안 숫자를 모두 꺼냄 (중첩 배열은 펼침)
bool json_numbers(const std::string &js, size_t from, const char *key, std::vector<double> &out, size_t *end = nullptr)
{
	const std::string k = std::string("\"") + key + "\"";
	size_t p = js.find(k, from);
	if (p == std::string::npos) return false;
	p = js.find('[', p + k.size());
	if (p == std::string::npos) return false;
	int depth = 0;
	out.clear();
	for (size_t i = p; i < js.size(); ++i) {
		const char c = js[i];
		if (c == '[') ++depth;
		else if (c == ']') { if (--depth == 0) { if (end) *end = i; return true; } }
		else if (c == '-' || c == '.' || (c >= '0' && c <= '9')) {
			char *e = nullptr;
			const double v = std::strtod(js.c_str() + i, &e);
			out.push_back(v);
			i = size_t(e - js.c_str()) - 1;
		}
	}
	return false;
}

bool json_int(const std::string &js, size_t from, const char *key, long long &out)
{
	const std::string k = std::string("\"") + key + "\"";
	size_t p = js.find(k, from);
	if (p == std::string::npos) return false;
	p = js.find(':', p + k.size());
	if (p == std::string::npos) return false;
	out = std::strtoll(js.c_str() + p + 1, nullptr, 10);
	return true;
}

// ---- 문장 정리 (supertonic 패키지와 같은 단계) ----
bool is_emoji(wchar_t hi, wchar_t lo, bool pair)
{
	if (pair) return true;		// 보조 평면(이모지 등)은 모델이 쓰지 않음
	return (hi >= 0x2600 && hi <= 0x27BF);
}

std::wstring preprocess(const std::wstring &in, const std::wstring &lang)
{
	// 1) NFKD (한글 음절 → 자모)
	std::wstring t;
	{
		// 한글 음절은 유니코드 표준 산식으로 직접 풀고(초성·중성·종성), 나머지는 Windows NormalizeString(NFKD)
		std::wstring h;
		for (wchar_t c : in) {
			if (c >= 0xAC00 && c <= 0xD7A3) {
				const int s = c - 0xAC00;
				h += wchar_t(0x1100 + s / 588);
				h += wchar_t(0x1161 + (s % 588) / 28);
				if (s % 28) h += wchar_t(0x11A7 + s % 28);
			}
			else h += c;
		}
		int cap = int(h.size()) * 4 + 16;
		for (int tries = 0; tries < 3 && t.empty(); ++tries) {
			std::wstring buf(size_t(cap), L'\0');
			const int n = NormalizeString(NormalizationKD, h.c_str(), int(h.size()), &buf[0], cap);
			if (n > 0) { buf.resize(size_t(n)); t = buf; }
			else if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) cap *= 2;
			else break;
		}
		if (t.empty()) t = h;
	}
	// 2) 이모지 빼기, 3) 기호 바꾸기, 4) 꾸밈 기호 빼기
	std::wstring u;
	for (size_t i = 0; i < t.size(); ++i) {
		wchar_t c = t[i];
		const bool pair = c >= 0xD800 && c <= 0xDBFF && i + 1 < t.size();
		if (is_emoji(c, pair ? t[i + 1] : 0, pair)) { if (pair) ++i; continue; }
		switch (c) {
		case 0x2013: case 0x2011: case 0x2014: c = L'-'; break;
		case 0x00AF: case L'_': case L'[': case L']': case L'|': case L'/': case L'#': case 0x2192: case 0x2190: c = L' '; break;
		case 0x201C: case 0x201D: c = L'"'; break;
		case 0x2018: case 0x2019: case 0x00B4: case L'`': c = L'\''; break;
		case 0x2665: case 0x2606: case 0x2661: case 0x00A9: case L'\\': continue;
		default: break;
		}
		if (c == L'@') { u += L" at "; continue; }
		u += c;
	}
	// 5) 문장 부호 앞 공백 없애기, 6) 같은 따옴표 반복 하나로, 7) 공백 정리
	std::wstring v;
	for (size_t i = 0; i < u.size(); ++i) {
		const wchar_t c = u[i];
		if (c == L' ' && i + 1 < u.size() && std::wstring(L",.!?;:'").find(u[i + 1]) != std::wstring::npos) continue;
		if ((c == L'"' || c == L'\'') && !v.empty() && v.back() == c) continue;
		v += c;
	}
	std::wstring w;
	bool sp = false;
	for (wchar_t c : v) {
		if (iswspace(c)) { sp = true; continue; }
		if (sp && !w.empty()) w += L' ';
		sp = false;
		w += c;
	}
	// 8) 끝에 마침표
	static const std::wstring ending = L".!?;:,'\")]}\x2026\x3002\x300D\x300F\x3011\x3009\x300B\x203A\x00BB";
	if (w.empty() || ending.find(w.back()) == std::wstring::npos) w += L'.';
	// 9) 언어 표시
	return L"<" + lang + L">" + w + L"</" + lang + L">";
}

// 긴 글은 문장 단위로 나눠 읽음 (한국어 120자)
std::vector<std::wstring> chunk_text(const std::wstring &text, size_t max_len)
{
	std::vector<std::wstring> sentences, chunks;
	std::wstring cur;
	for (size_t i = 0; i < text.size(); ++i) {
		cur += text[i];
		const bool end = (text[i] == L'.' || text[i] == L'!' || text[i] == L'?' || text[i] == L'\n') && (i + 1 >= text.size() || iswspace(text[i + 1]));
		if (end) { sentences.push_back(cur); cur.clear(); }
	}
	if (!cur.empty()) sentences.push_back(cur);
	std::wstring c;
	for (std::wstring s : sentences) {
		while (!s.empty() && iswspace(s.front())) s.erase(0, 1);
		if (s.empty()) continue;
		if (!c.empty() && c.size() + 1 + s.size() > max_len) { chunks.push_back(c); c.clear(); }
		while (s.size() > max_len) {		// 한 문장이 너무 길면 쉼표·공백에서 자름
			size_t cut = s.find_last_of(L", ", max_len);
			if (cut == std::wstring::npos || cut < max_len / 2) cut = max_len;
			if (!c.empty()) { chunks.push_back(c); c.clear(); }
			chunks.push_back(s.substr(0, cut + 1));
			s = s.substr(cut + 1);
			while (!s.empty() && iswspace(s.front())) s.erase(0, 1);
		}
		if (!s.empty()) c = c.empty() ? s : c + L" " + s;
	}
	if (!c.empty()) chunks.push_back(c);
	return chunks;
}

// 44.1kHz → 8kHz (창 sinc 저역 통과, 차단 3.6kHz)
std::vector<float> resample_to_8k(const std::vector<float> &in, int sr)
{
	const double ratio = double(sr) / 8000.0, fc = 3600.0 / sr;
	const int half = 96;
	const size_t n_out = size_t(double(in.size()) / ratio);
	std::vector<float> out(n_out);
	for (size_t n = 0; n < n_out; ++n) {
		const double x = n * ratio;
		const long c = long(std::floor(x));
		double acc = 0, wsum = 0;
		for (long k = c - half + 1; k <= c + half; ++k) {
			const double d = x - double(k);
			const double win = 0.5 + 0.5 * std::cos(M_PI * d / half);
			const double h = (std::fabs(d) < 1e-9 ? 2 * fc : std::sin(2 * M_PI * fc * d) / (M_PI * d)) * win;
			wsum += h;
			if (k >= 0 && size_t(k) < in.size()) acc += in[size_t(k)] * h;
		}
		out[n] = float(wsum != 0 ? acc / wsum : 0);
	}
	return out;
}

bool write_wav8k(const std::wstring &path, const std::vector<float> &pcm)
{
	float peak = 1e-6f;
	for (float v : pcm) peak = std::max(peak, std::fabs(v));
	const float g = peak > 0.95f ? 0.95f / peak : 1.0f;
	std::ofstream f(path.c_str(), std::ios::binary);
	if (!f) return false;
	auto u32 = [&](uint32_t x) { f.write(reinterpret_cast<const char *>(&x), 4); };
	auto u16 = [&](uint16_t x) { f.write(reinterpret_cast<const char *>(&x), 2); };
	const uint32_t bytes = uint32_t(pcm.size() * 2);
	f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
	f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(8000); u32(16000); u16(2); u16(16);
	f.write("data", 4); u32(bytes);
	for (float v : pcm) { const int s = int(std::lround(std::max(-1.0f, std::min(1.0f, v * g)) * 32767)); u16(uint16_t(int16_t(s))); }
	return bool(f);
}

// ---- ONNX Runtime ----
const OrtApi *g_api = nullptr;
OrtEnv *g_env = nullptr;
OrtMemoryInfo *g_mem = nullptr;

bool ok(OrtStatus *st, const char *what)
{
	if (!st) return true;
	std::fprintf(stderr, "ERROR %s: %s\n", what, g_api->GetErrorMessage(st));
	g_api->ReleaseStatus(st);
	return false;
}

struct Model {
	OrtSession *s = nullptr;
	std::string out_name;
	bool load(const std::wstring &path, int threads)
	{
		OrtSessionOptions *so = nullptr;
		if (!ok(g_api->CreateSessionOptions(&so), "options")) return false;
		g_api->SetIntraOpNumThreads(so, threads);
		g_api->SetSessionGraphOptimizationLevel(so, ORT_ENABLE_ALL);
		const bool r = ok(g_api->CreateSession(g_env, path.c_str(), so, &s), "load model");
		g_api->ReleaseSessionOptions(so);
		if (!r) return false;
		OrtAllocator *al = nullptr;
		g_api->GetAllocatorWithDefaultOptions(&al);
		char *name = nullptr;
		if (!ok(g_api->SessionGetOutputName(s, 0, al, &name), "output name")) return false;
		out_name = name;
		al->Free(al, name);
		return true;
	}
};

struct Tensor {
	OrtValue *v = nullptr;
	~Tensor() { if (v) g_api->ReleaseValue(v); }
};

OrtValue *make_f32(std::vector<float> &d, std::vector<int64_t> shape)
{
	OrtValue *v = nullptr;
	ok(g_api->CreateTensorWithDataAsOrtValue(g_mem, d.data(), d.size() * sizeof(float), shape.data(), shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &v), "tensor");
	return v;
}
OrtValue *make_i64(std::vector<int64_t> &d, std::vector<int64_t> shape)
{
	OrtValue *v = nullptr;
	ok(g_api->CreateTensorWithDataAsOrtValue(g_mem, d.data(), d.size() * sizeof(int64_t), shape.data(), shape.size(), ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64, &v), "tensor");
	return v;
}

// 실행 → 첫 출력 (데이터 복사 + 모양)
bool run(Model &m, std::vector<const char *> names, std::vector<OrtValue *> ins, std::vector<float> &out, std::vector<int64_t> &shape)
{
	OrtValue *o = nullptr;
	const char *on = m.out_name.c_str();
	if (!ok(g_api->Run(m.s, nullptr, names.data(), ins.data(), ins.size(), &on, 1, &o), "run")) return false;
	OrtTensorTypeAndShapeInfo *info = nullptr;
	g_api->GetTensorTypeAndShape(o, &info);
	size_t nd = 0, cnt = 0;
	g_api->GetDimensionsCount(info, &nd);
	shape.assign(nd, 0);
	g_api->GetDimensions(info, shape.data(), nd);
	g_api->GetTensorShapeElementCount(info, &cnt);
	g_api->ReleaseTensorTypeAndShapeInfo(info);
	float *p = nullptr;
	g_api->GetTensorMutableData(o, reinterpret_cast<void **>(&p));
	out.assign(p, p + cnt);
	g_api->ReleaseValue(o);
	return true;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
	std::wstring model, voice = L"F1", in, outp, lang = L"ko";
	double speed = 1.05;
	int steps = 8;
	bool list = false;
	for (int i = 1; i < argc; ++i) {
		const std::wstring a = argv[i];
		auto next = [&]() { return i + 1 < argc ? std::wstring(argv[++i]) : std::wstring(); };
		if (a == L"--model") model = next();
		else if (a == L"--voice") voice = next();
		else if (a == L"--in") in = next();
		else if (a == L"--out") outp = next();
		else if (a == L"--lang") lang = next();
		else if (a == L"--speed") speed = _wtof(next().c_str());
		else if (a == L"--steps") steps = _wtoi(next().c_str());
		else if (a == L"--list") list = true;
	}
	if (model.empty()) { std::fprintf(stderr, "usage: dvbridge_tts --model <dir> (--list | --voice F1 --in text.txt --out out.wav)\n"); return 2; }
	if (list) {
		WIN32_FIND_DATAW fd;
		HANDLE h = FindFirstFileW((model + L"\\voice_styles\\*.json").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) return 1;
		do {
			std::wstring n = fd.cFileName;
			n = n.substr(0, n.size() - 5);
			std::printf("VOICE|%ls\n", n.c_str());
		} while (FindNextFileW(h, &fd));
		FindClose(h);
		return 0;
	}
	if (in.empty() || outp.empty()) return 2;
	speed = std::max(0.7, std::min(2.0, speed));
	steps = std::max(1, std::min(16, steps));

	// 설정·문자표·목소리
	std::string cfg, idx_js, style_js, text_u8;
	if (!read_file(model + L"\\onnx\\tts.json", cfg) || !read_file(model + L"\\onnx\\unicode_indexer.json", idx_js) ||
		!read_file(model + L"\\voice_styles\\" + voice + L".json", style_js)) { std::fprintf(stderr, "ERROR model files not found\n"); return 1; }
	if (!read_file(in, text_u8)) { std::fprintf(stderr, "ERROR input not found\n"); return 1; }
	long long sr = 44100, base = 512, ccf = 6, ldim = 24;
	const size_t ttl_at = cfg.find("\"ttl\"");
	json_int(cfg, 0, "sample_rate", sr);
	json_int(cfg, 0, "base_chunk_size", base);
	if (ttl_at != std::string::npos) { json_int(cfg, ttl_at, "chunk_compress_factor", ccf); json_int(cfg, ttl_at, "latent_dim", ldim); }
	std::vector<double> idx_d;
	if (!idx_js.empty() && idx_js[0] == '[') { idx_js = "{\"i\":" + idx_js + "}"; }
	if (!json_numbers(idx_js, 0, "i", idx_d)) { std::fprintf(stderr, "ERROR unicode indexer\n"); return 1; }
	std::vector<double> ttl_d, ttl_dims, dp_d, dp_dims;
	size_t ttl_pos = style_js.find("\"style_ttl\""), dp_pos = style_js.find("\"style_dp\"");
	if (ttl_pos == std::string::npos || dp_pos == std::string::npos ||
		!json_numbers(style_js, ttl_pos, "data", ttl_d) || !json_numbers(style_js, ttl_pos, "dims", ttl_dims) ||
		!json_numbers(style_js, dp_pos, "data", dp_d) || !json_numbers(style_js, dp_pos, "dims", dp_dims)) { std::fprintf(stderr, "ERROR voice style\n"); return 1; }
	std::vector<float> style_ttl(ttl_d.begin(), ttl_d.end()), style_dp(dp_d.begin(), dp_d.end());
	std::vector<int64_t> ttl_shape(ttl_dims.begin(), ttl_dims.end()), dp_shape(dp_dims.begin(), dp_dims.end());

	// ONNX Runtime (같은 폴더의 onnxruntime.dll)
	wchar_t exe[MAX_PATH];
	GetModuleFileNameW(nullptr, exe, MAX_PATH);
	std::wstring dir = exe;
	dir = dir.substr(0, dir.find_last_of(L"\\/"));
	HMODULE ort = LoadLibraryExW((dir + L"\\onnxruntime.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!ort) { std::fprintf(stderr, "ERROR onnxruntime.dll not found\n"); return 1; }
	typedef const OrtApiBase *(ORT_API_CALL * GetBase)();
	GetBase gb = reinterpret_cast<GetBase>(GetProcAddress(ort, "OrtGetApiBase"));
	if (!gb || !(g_api = gb()->GetApi(ORT_API_VERSION))) { std::fprintf(stderr, "ERROR onnxruntime version\n"); return 1; }
	if (!ok(g_api->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "dvbridge_tts", &g_env), "env")) return 1;
	if (!ok(g_api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &g_mem), "mem")) return 1;
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	const int threads = std::max(1, std::min(8, int(si.dwNumberOfProcessors) - 1));
	Model dp, te, ve, voc;
	if (!dp.load(model + L"\\onnx\\duration_predictor.onnx", threads) || !te.load(model + L"\\onnx\\text_encoder.onnx", threads) ||
		!ve.load(model + L"\\onnx\\vector_estimator.onnx", threads) || !voc.load(model + L"\\onnx\\vocoder.onnx", threads)) return 1;

	std::mt19937 rng(std::random_device{}());
	std::normal_distribution<float> nd(0.0f, 1.0f);
	std::vector<float> all;
	const std::vector<std::wstring> chunks = chunk_text(widen(text_u8), lang == L"ko" ? 120 : 300);
	for (size_t ci = 0; ci < chunks.size(); ++ci) {
		const std::wstring t = preprocess(chunks[ci], lang);
		std::vector<int64_t> ids;
		for (wchar_t c : t) {
			const size_t u = size_t(c);
			if (u < idx_d.size() && idx_d[u] >= 0) ids.push_back(int64_t(idx_d[u]));
		}
		if (ids.empty()) continue;
		const int64_t L = int64_t(ids.size());
		std::vector<float> tmask(size_t(L), 1.0f);
		Tensor t_ids{make_i64(ids, {1, L})}, t_mask{make_f32(tmask, {1, 1, L})}, t_dp{make_f32(style_dp, dp_shape)}, t_ttl{make_f32(style_ttl, ttl_shape)};
		std::vector<float> dur, emb;
		std::vector<int64_t> dur_shape, emb_shape;
		if (!run(dp, {"text_ids", "style_dp", "text_mask"}, {t_ids.v, t_dp.v, t_mask.v}, dur, dur_shape) || dur.empty()) return 1;
		const double seconds = dur[0] / speed;
		if (_wgetenv(L"DVB_TTS_DEBUG")) std::fprintf(stderr, "chunk %zu: chars %zu ids %lld dur %.2f\n", ci, t.size(), (long long)L, seconds);
		if (!run(te, {"text_ids", "style_ttl", "text_mask"}, {t_ids.v, t_ttl.v, t_mask.v}, emb, emb_shape)) return 1;
		const int64_t wav_len = int64_t(seconds * double(sr));
		const int64_t chunk = base * ccf, lat_len = std::max<int64_t>(1, (wav_len + chunk - 1) / chunk), lat_dim = ldim * ccf;
		std::vector<float> xt(size_t(lat_dim * lat_len)), lmask(size_t(lat_len), 1.0f);
		for (float &v : xt) v = nd(rng);
		Tensor t_emb{make_f32(emb, emb_shape)}, t_lmask{make_f32(lmask, {1, 1, lat_len})};
		std::vector<float> total{float(steps)};
		Tensor t_total{make_f32(total, {1})};
		for (int s = 0; s < steps; ++s) {
			std::vector<float> cur{float(s)}, nxt;
			std::vector<int64_t> nshape;
			Tensor t_xt{make_f32(xt, {1, lat_dim, lat_len})}, t_cur{make_f32(cur, {1})};
			if (!run(ve, {"noisy_latent", "text_emb", "style_ttl", "text_mask", "latent_mask", "current_step", "total_step"},
					 {t_xt.v, t_emb.v, t_ttl.v, t_mask.v, t_lmask.v, t_cur.v, t_total.v}, nxt, nshape)) return 1;
			xt.swap(nxt);
		}
		std::vector<float> wav;
		std::vector<int64_t> wshape;
		Tensor t_lat{make_f32(xt, {1, lat_dim, lat_len})};
		if (!run(voc, {"latent"}, {t_lat.v}, wav, wshape)) return 1;
		if (int64_t(wav.size()) > wav_len && wav_len > 0) wav.resize(size_t(wav_len));
		if (ci) all.insert(all.end(), size_t(0.3 * double(sr)), 0.0f);		// 문장 사이 0.3초
		all.insert(all.end(), wav.begin(), wav.end());
	}
	if (all.empty()) { std::fprintf(stderr, "ERROR nothing to say\n"); return 1; }
	if (!write_wav8k(outp, resample_to_8k(all, int(sr)))) { std::fprintf(stderr, "ERROR write\n"); return 1; }
	std::printf("OK %.1fs\n", double(all.size()) / double(sr));
	return 0;
}
