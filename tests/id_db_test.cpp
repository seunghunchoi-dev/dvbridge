// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
// RadioID 명단 해석 시험 (인터넷 없이 작은 CSV로)
#include <cstdio>
#include <QCoreApplication>
#include "session/id_db.h"
#ifdef _WIN32
#include <windows.h>
#endif

static int g_fail = 0;
#define CHECK(c, msg) do { if (c) std::printf("  [OK]   %s\n", msg); else { ++g_fail; std::printf("  [FAIL] %s\n", msg); } } while (0)

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
#endif
	const QByteArray csv =
		"RADIO_ID,CALLSIGN,FIRST_NAME,LAST_NAME,CITY,STATE,COUNTRY\r\n"
		"4500001,hl0exm,Hong,,Seoul,Seoul,Korea Republic of\r\n"
		"1023007,VA3BOC,Hans Juergen,,Cornwall,Ontario,Canada\n"
		"2341234,G0ABC,\"Smith, John\",,\"London, City\",,United Kingdom\n"
		"bad,line\n"
		"99999999,TOOBIG,X,,,,\n"
		"\n";
	IdDatabase db;
	CHECK(db.load_data(csv), "명단 해석");
	CHECK(db.size() == 3, "올바른 3건만 (잘못된 줄·24비트 넘는 ID 제외)");
	CHECK(db.display(4500001) == "HL0EXM [Hong] (4500001)", "BM 형식 표시: 콜사인 대문자 + 이름");
	CHECK(db.display(1023007) == "VA3BOC [Hans] (1023007)", "이름은 첫 단어만");
	CHECK(db.find(2341234) && db.find(2341234)->name == "Smith,", "따옴표 안 쉼표 처리");
	CHECK(db.display(1234567) == "1234567", "명단에 없으면 ID만");
	IdDatabase bad;
	CHECK(!bad.load_data("hello,world\n1,2,3\n"), "RadioID 형식이 아니면 거부");
	std::printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
	return g_fail ? 1 : 0;
}
