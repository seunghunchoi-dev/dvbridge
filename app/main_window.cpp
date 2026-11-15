// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
#include "main_window.h"
#include "net_check.h"
#include "notice_service.h"
#include "audio/usrp_port.h"
#include "net/id_db_updater.h"
#include "web/dashboard_server.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifndef DVBRIDGE_VERSION
#define DVBRIDGE_VERSION "0.1"
#endif

namespace {
enum Col { C_CALL, C_HOTSPOT, C_USRP, C_MASTER, C_PHONE, C_TARGET, C_AIR, C_COUNT };
const char *kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr int kWatchdogMs = 15000;

QStringList lan_ipv4()
{
	QStringList a;
	for (const QHostAddress &h : QNetworkInterface::allAddresses()) {
		if (h.protocol() != QAbstractSocket::IPv4Protocol || h.isLoopback()) continue;
		const QString s = h.toString();
		if (!s.startsWith("169.254.")) a << s;
	}
	return a;
}
}

QString MainWindow::clean_exit_flag() { return QDir(AppSettings::config_dir()).filePath("clean_exit"); }

MainWindow::MainWindow(bool start_hidden, bool recover, QWidget *parent) : QMainWindow(parent)
{
	setWindowTitle("DVBridge");
	app_ = AppSettings::load();
	rows_.resize(app_.users.size());
	QFile::remove(clean_exit_flag());
	connect(qApp, &QCoreApplication::aboutToQuit, this, [] {	// 정상 종료(로그오프·종료 포함) 표시
		QFile f(clean_exit_flag());
		if (f.open(QIODevice::WriteOnly)) f.write("1");
	});

	build_ui();
	build_tray();

	append_log("DVBridge " DVBRIDGE_VERSION " - 최초 제작: 최승훈 (6K2KZR), MIT License");
	append_log("설정 폴더: " + QDir::toNativeSeparators(AppSettings::config_dir()));
	if (!AppSettings::config_note().isEmpty()) append_log(AppSettings::config_note());
	if (recover) append_log("와치독: 비정상 종료 뒤 다시 실행했습니다");

	calls_ = new CallLog(this);
	calls_->set_file(QDir(AppSettings::config_dir()).filePath("history.jsonl"));
	web_ = new DashboardServer(calls_, this);
	DashboardServer::AdminHooks hooks;
	hooks.has_password = [this] { return AppSettings::has_admin_password(app_); };
	hooks.check_password = [this](const QString &pw) { return AppSettings::check_admin_password(app_, pw); };
	hooks.set_password = [this](const QString &pw) {
		AppSettings::set_admin_password(app_, pw);
		AppSettings::save(app_);
		append_log("웹 설정 관리자 비밀번호를 정했습니다");
		refresh_status();
	};
	hooks.config = [this] { return web_config(); };
	hooks.action = [this](const QString &a, const QJsonObject &b) { return web_action(a, b); };
	hooks.backup = [] {
		QFile f(AppSettings::config_path());
		return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
	};
	hooks.notice_wav = [this] { return notice_wav_; };
	web_->set_admin_hooks(hooks);
	notice_ = new NoticeService(this);
	notice_->set_config(app_.notice);
	connect(notice_, &NoticeService::log, this, &MainWindow::append_log);
	connect(notice_, &NoticeService::ready, this, [this](QVector<int16_t> pcm, QString title, QString source, QStringList targets, bool ai) {
		notice_wav_ = NoticeService::make_wav(pcm);		// 웹에서 PC 스피커로 들을 수 있게 보관
		++notice_wav_id_;
		if (source == "preview") { append_log("공지 미리 듣기 음성 만듦: " + title.left(40)); return; }	// 휴대폰에는 안 보냄
		if (!notice_dispatch(pcm, title, source, targets, ai)) append_log("공지: 보낼 사용자가 없습니다 (서비스가 꺼져 있거나 대상 사용자가 접속 중이 아님)");
	});
	web_->set_status_provider([this] {
		QJsonObject o;
		o["iddb_count"] = ids_.size();
		o["server"] = sysmon_.to_json();
		o["listeners"] = web_->listener_count();
		o["service_on"] = service_on_;
		if (ids_.size()) o["iddb_date"] = ids_.file_time().toString("yyyy-MM-dd");
		QJsonArray users;
		for (size_t i = 0; i < rows_.size(); ++i) {
			const SessionConfig &c = app_.users[int(i)];
			const Row &r = rows_[i];
			QJsonObject u;
			u["call"] = c.master.callsign;
			u["enabled"] = c.enabled;
			u["hotspot"] = double(c.master.id);
			u["usrp_port"] = c.usrp_port;
			u["master"] = c.master.host;
			u["target"] = r.session ? r.session->config().tg : c.tg;
			u["target_priv"] = r.session ? r.session->config().priv : c.priv;
			u["slot"] = c.slot;
			if (r.session) {
				u["master_state"] = r.session->master_state_text();
				u["connected"] = r.session->master()->connected();
				u["client"] = r.session->client_address();
				u["client_net"] = NetKind::of_text(r.session->client_address());	// 휴대폰이 내부망/외부망 어디서 접속했는지
				{	const ClientInfo &ci = r.session->client_info();	// 클라이언트가 보낸 내 정보 (DVBridge 클라이언트)
					if (ci.valid()) u["client_info"] = QJsonObject{{"call", ci.call}, {"dmr_id", qint64(ci.dmr_id)}, {"name", ci.name}, {"device", ci.device}, {"app", ci.app}};
				}
				u["short_tx"] = r.session->short_tx_in_window();
				CallRecord a;
				if (r.session->active_call(a)) u["active"] = a.to_json();
			}
			users.append(u);
		}
		o["users"] = users;
		return o;
	});
	start_dashboard();

	auto *tick = new QTimer(this);		// 1초마다: 서버 부하 측정 + 상태 표시
	connect(tick, &QTimer::timeout, this, [this] { sysmon_.sample(); refresh_status(); });
	tick->start(1000);
	auto *wd = new QTimer(this);		// 와치독
	connect(wd, &QTimer::timeout, this, &MainWindow::watchdog_tick);
	wd->start(kWatchdogMs);
	if (app_.watchdog) spawn_watchdog();

	load_id_db();
	refresh_table();
	refresh_status();
	const bool resume = QFile::exists(resume_flag());
	if (resume) { QFile::remove(resume_flag()); append_log("업데이트 전에 켜져 있던 서비스를 다시 켭니다"); }
	const bool want = ((recover || resume) && app_.service_on) || (app_.auto_service && !app_.users.isEmpty());
	QTimer::singleShot(400, this, [this, want, start_hidden] {
		check_vocoder(!start_hidden);
		if (want) set_service(true);
	});
	if (!start_hidden) show();
}

MainWindow::~MainWindow() { stop_all(); }

// ===================== 화면 =====================

void MainWindow::build_ui()
{
	auto *central = new QWidget;
	auto *root = new QVBoxLayout(central);
	root->setSpacing(10);

	auto *title = new QLabel("<b style='font-size:20pt;color:#c0392b'>DVBridge</b> <span style='color:gray'>" DVBRIDGE_VERSION "</span>");
	root->addWidget(title);

	// 서비스 상태 + 큰 켜기/끄기
	auto *srow = new QHBoxLayout;
	status_ = new QLabel;
	status_->setWordWrap(true);
	service_btn_ = new QPushButton;
	service_btn_->setMinimumSize(200, 60);
	QFont bf = service_btn_->font(); bf.setPointSize(bf.pointSize() + 3); bf.setBold(true); service_btn_->setFont(bf);
	srow->addWidget(status_, 1);
	srow->addWidget(service_btn_);
	root->addLayout(srow);

	// 처음 사용 안내
	guide_ = new QLabel;
	guide_->setWordWrap(true);
	guide_->setStyleSheet("background:#fff3cd;color:#5c4400;border:1px solid #f0d080;border-radius:6px;padding:10px;");
	root->addWidget(guide_);

	// 웹 설정·대시보드
	auto *wrow = new QHBoxLayout;
	auto *webset = new QPushButton("웹 설정 열기");
	auto *dash = new QPushButton("대시보드 열기");
	for (QPushButton *b : {webset, dash}) { b->setMinimumHeight(44); QFont f = b->font(); f.setPointSize(f.pointSize() + 1); b->setFont(f); }
	webset->setToolTip("사용자 추가·편집, 백업, 명단 받기 등 모든 설정 (관리자 비밀번호 로그인)");
	dash->setToolTip("송수신 상태·이력, 실시간 듣기, 서버 부하");
	wrow->addWidget(webset, 1);
	wrow->addWidget(dash, 1);
	root->addLayout(wrow);
	urls_ = new QLabel;
	urls_->setWordWrap(true);
	urls_->setTextInteractionFlags(Qt::TextBrowserInteraction);
	urls_->setOpenExternalLinks(true);
	root->addWidget(urls_);

	// 접속 IP·포트
	auto *net = new QGroupBox("웹 접속 주소");
	auto *ng = new QGridLayout(net);
	bind_ = new QComboBox;
	bind_->addItem("모든 네트워크 (휴대폰·다른 PC에서도 접속)", "0.0.0.0");
	bind_->addItem("이 PC만 (127.0.0.1)", "127.0.0.1");
	for (const QString &ip : lan_ipv4()) bind_->addItem("이 주소만: " + ip, ip);
	int bi = bind_->findData(app_.web_bind);
	if (bi < 0) { bind_->addItem("이 주소만: " + app_.web_bind + " (지금 없음)", app_.web_bind); bi = bind_->count() - 1; }
	bind_->setCurrentIndex(bi);
	port_ = new QSpinBox;
	port_->setRange(1024, 65535);
	port_->setValue(app_.web_port);
	port_->setMinimumWidth(120);
	auto *apply = new QPushButton("적용");
	ng->addWidget(new QLabel("IP"), 0, 0);
	ng->addWidget(bind_, 0, 1);
	ng->addWidget(new QLabel("포트"), 0, 2);
	ng->addWidget(port_, 0, 3);
	ng->addWidget(apply, 0, 4);
	ng->setColumnStretch(1, 1);
	net_hint_ = new QLabel;
	net_hint_->setWordWrap(true);
	ng->addWidget(net_hint_, 1, 0, 1, 5);
	root->addWidget(net);

	// 옵션
	auto *opt = new QGroupBox("자동 실행·복구");
	auto *ov = new QVBoxLayout(opt);
	autostart_ = new QCheckBox("Windows를 켜면 DVBridge 자동 실행 (트레이로 시작)");
	auto_service_ = new QCheckBox("DVBridge가 켜지면 서비스 자동 시작");
	watchdog_ = new QCheckBox("와치독 — 접속이 끊기거나 프로그램이 비정상 종료되면 자동 복구");
	autostart_->setChecked(autostart_enabled());
	auto_service_->setChecked(app_.auto_service);
	watchdog_->setChecked(app_.watchdog);
	for (QCheckBox *c : {autostart_, auto_service_, watchdog_}) ov->addWidget(c);
	root->addWidget(opt);

	// 사용자 상태 (보기 전용)
	table_ = new QTableWidget(0, C_COUNT);
	table_->setHorizontalHeaderLabels({"콜사인", "핫스팟 ID", "USRP 포트", "BM 접속", "휴대폰", "대상", "지금"});
	table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(C_AIR, QHeaderView::Stretch);
	table_->setSelectionMode(QAbstractItemView::NoSelection);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table_->verticalHeader()->setVisible(false);
	table_->setMinimumHeight(110);
	root->addWidget(table_, 1);

	auto *brow = new QHBoxLayout;
	auto *showlog = new QPushButton("기록 보기");
	showlog->setCheckable(true);
	auto *folder = new QPushButton("설정 폴더");
	auto *about = new QPushButton("정보");
	auto *quit = new QPushButton("종료");
	brow->addWidget(showlog);
	brow->addStretch(1);
	for (QPushButton *b : {folder, about, quit}) brow->addWidget(b);
	root->addLayout(brow);
	log_ = new QPlainTextEdit;
	log_->setReadOnly(true);
	log_->setMaximumBlockCount(5000);
	log_->setVisible(false);
	root->addWidget(log_, 2);
	setCentralWidget(central);
	resize(820, 720);

	// 키보드 초점을 받지 않게: 다른 창에 글을 쓰다 Enter·스페이스가 이 창으로 오면 서비스가 꺼지는 일이 있었음
	service_btn_->setFocusPolicy(Qt::NoFocus);
	quit->setFocusPolicy(Qt::NoFocus);
	connect(service_btn_, &QPushButton::clicked, this, [this] {
		append_log(service_on_ ? "(창 버튼) 서비스 끄기" : "(창 버튼) 서비스 켜기");
		QString err;
		if (!set_service(!service_on_, &err) && !err.isEmpty()) QMessageBox::warning(this, "DVBridge", err);
	});
	connect(webset, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(local_url("settings"))); });
	connect(dash, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(local_url())); });
	connect(apply, &QPushButton::clicked, this, &MainWindow::apply_network);
	connect(autostart_, &QCheckBox::toggled, this, [this](bool on) {
		set_autostart(on);
		append_log(on ? "Windows 자동 실행 켬" : "Windows 자동 실행 끔");
	});
	connect(auto_service_, &QCheckBox::toggled, this, [this](bool on) { app_.auto_service = on; AppSettings::save(app_); });
	connect(watchdog_, &QCheckBox::toggled, this, [this](bool on) {
		app_.watchdog = on;
		AppSettings::save(app_);
		append_log(on ? "와치독 켬" : "와치독 끔");
		if (on) spawn_watchdog();
	});
	connect(showlog, &QPushButton::toggled, log_, &QWidget::setVisible);
	connect(folder, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(AppSettings::config_dir())); });
	connect(about, &QPushButton::clicked, this, &MainWindow::show_about);
	connect(quit, &QPushButton::clicked, this, &MainWindow::quit_app);
}

void MainWindow::build_tray()
{
	tray_ = new QSystemTrayIcon(QIcon(":/dvbridge.png"), this);
	tray_->setToolTip("DVBridge");
	auto *menu = new QMenu(this);
	menu->addAction("열기", this, [this] { showNormal(); raise(); activateWindow(); });
	menu->addAction("웹 설정 열기", this, [this] { QDesktopServices::openUrl(QUrl(local_url("settings"))); });
	menu->addAction("대시보드 열기", this, [this] { QDesktopServices::openUrl(QUrl(local_url())); });
	menu->addSeparator();
	tray_service_ = menu->addAction("서비스 켜기", this, [this] {
		append_log(service_on_ ? "(트레이 메뉴) 서비스 끄기" : "(트레이 메뉴) 서비스 켜기");
		QString err;
		if (!set_service(!service_on_, &err) && !err.isEmpty()) tray_->showMessage("DVBridge", err, QSystemTrayIcon::Warning);
	});
	menu->addSeparator();
	menu->addAction("종료", this, &MainWindow::quit_app);
	tray_->setContextMenu(menu);
	connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
		if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) { showNormal(); raise(); activateWindow(); }
	});
	tray_->show();
}

void MainWindow::refresh_status()
{
	if (!status_) return;
	int conn = 0, phones = 0, on = 0;
	for (size_t i = 0; i < rows_.size(); ++i) {
		const Row &r = rows_[i];
		if (!r.session) continue;
		++on;
		conn += r.session->master()->connected();
		phones += !r.session->client_address().isEmpty();
	}
	QString s;
	if (service_on_) s = QString("<span style='font-size:15pt;color:#1e8449'>● 서비스 켜짐</span><br>사용자 %1명 · BM 접속 %2명 · 휴대폰 연결 %3대").arg(on).arg(conn).arg(phones);
	else s = QString("<span style='font-size:15pt;color:gray'>○ 서비스 꺼짐</span><br>사용자 %1명 — [서비스 켜기]를 누르면 접속합니다").arg(app_.users.size());
	status_->setText(s);
	service_btn_->setText(service_on_ ? "서비스 끄기" : "서비스 켜기");
	service_btn_->setStyleSheet(service_on_ ? "QPushButton{background:#c0392b;color:white;border-radius:8px;}" : "QPushButton{background:#1e8449;color:white;border-radius:8px;}");
	if (tray_service_) tray_service_->setText(service_on_ ? "서비스 끄기" : "서비스 켜기");
	if (tray_) tray_->setToolTip(service_on_ ? QString("DVBridge — 서비스 켜짐 (BM 접속 %1명)").arg(conn) : QString("DVBridge — 서비스 꺼짐"));

	QString g;
	if (!AppSettings::has_admin_password(app_)) g = "<b>처음 사용하시나요?</b><br>① 아래 <b>[웹 설정 열기]</b>를 누르세요<br>② 웹 화면에서 <b>관리자 비밀번호</b>를 정하세요<br>③ <b>사용자 추가</b>로 콜사인·DMR ID·핫스팟 비밀번호를 넣은 뒤 <b>[서비스 켜기]</b>";
	else if (app_.users.isEmpty()) g = "<b>[웹 설정 열기]</b>에서 <b>사용자 추가</b>를 해 주세요. 그다음 <b>[서비스 켜기]</b>를 누르면 됩니다.";
	else if (!web_ok_) g = QString("<b>웹 화면을 열 수 없습니다</b> — 포트 %1을 다른 프로그램이 쓰고 있습니다. 아래 [웹 접속 주소]에서 포트를 바꾸고 [적용]을 누르세요.").arg(app_.web_port);
	guide_->setText(g);
	guide_->setVisible(!g.isEmpty());

	QStringList links;
	for (const QString &u : access_urls("settings")) links << QString("<a href='%1'>%1</a>").arg(u);
	urls_->setText(web_ok_ ? "다른 기기(휴대폰 등)에서 웹 설정: " + (links.isEmpty() ? QString("이 PC에서만 열 수 있음") : links.join(" , ")) : QString());
	QStringList inner, outer;
	for (const QString &ip : lan_ipv4()) (NetKind::is_public(QHostAddress(ip)) ? outer : inner) << ip;
	QStringList where;
	if (!inner.isEmpty()) where << "같은 와이파이(내부망): " + inner.join(", ");
	if (!outer.isEmpty()) where << "외부망(공인 IP): " + outer.join(", ");
	net_hint_->setText(QString("휴대폰 DVSwitch Mobile: Hostname = %1 · TX Port = 그 사용자의 USRP 포트 · Port = 34001")
					   .arg(where.isEmpty() ? QString("PC 주소를 찾지 못함") : where.join(" / ")));
	refresh_table();
}

void MainWindow::refresh_table()
{
	table_->setRowCount(int(app_.users.size()));
	for (int i = 0; i < app_.users.size(); ++i) refresh_row(i);
}

void MainWindow::refresh_row(int i)
{
	const SessionConfig &c = app_.users[i];
	const Row &r = rows_[size_t(i)];
	const int tg = r.session ? r.session->config().tg : c.tg;
	const bool priv = r.session ? r.session->config().priv : c.priv;
	const QString state = !c.enabled ? QString("사용 안 함") : r.master_state;
	const QStringList v = {c.master.callsign, QString::number(c.master.id), QString::number(c.usrp_port), state, r.client,
						   QString("%1 %2 · TS%3").arg(tg).arg(priv ? "개인" : "그룹").arg(c.slot), r.air};
	for (int col = 0; col < C_COUNT; ++col) {
		QTableWidgetItem *it = table_->item(i, col);
		if (!it) { it = new QTableWidgetItem; table_->setItem(i, col, it); }
		it->setText(v[col]);
	}
	const bool connected = r.session && r.session->master()->connected();
	table_->item(i, C_MASTER)->setForeground(connected ? QColor(0, 150, 0) : palette().text().color());
	QColor air;
	if (r.air.startsWith("송신") || r.air.startsWith("Unlink")) air = QColor(192, 57, 43);
	else if (r.air.startsWith("수신")) air = QColor(39, 174, 96);
	table_->item(i, C_AIR)->setBackground(air.isValid() ? air : QColor(Qt::transparent));
	table_->item(i, C_AIR)->setForeground(air.isValid() ? QColor(Qt::white) : palette().text().color());
}

void MainWindow::append_log(const QString &s)
{
	const QDateTime now = QDateTime::currentDateTime();
	const QString line = now.toString("HH:mm:ss ") + s;
	if (log_) log_->appendPlainText(line);
	// 파일에도 기록: data\logs\yyyy-MM-dd.log (창을 닫아도 원인 확인 가능)
	const QString dir = QDir(AppSettings::config_dir()).filePath("logs");
	QDir().mkpath(dir);
	QFile f(QDir(dir).filePath(now.toString("yyyy-MM-dd") + ".log"));
	if (f.open(QIODevice::Append | QIODevice::Text)) f.write((line + "\n").toUtf8());
}

void MainWindow::apply_network()
{
	const QString bind = bind_->currentData().toString();
	const int port = port_->value();
	for (const SessionConfig &c : app_.users)
		if (c.usrp_port == port) { QMessageBox::warning(this, "DVBridge", QString("포트 %1은 사용자 %2의 USRP 포트와 겹칩니다.").arg(port).arg(c.master.callsign)); return; }
	app_.web_bind = bind;
	app_.web_port = quint16(port);
	AppSettings::save(app_);
	start_dashboard();
	refresh_status();
	if (!web_ok_) QMessageBox::warning(this, "DVBridge", QString("포트 %1을 열 수 없습니다. 다른 포트를 골라 주세요.").arg(port));
}

void MainWindow::show_about()
{
	QMessageBox::about(this, "DVBridge 정보",
		"<h3>DVBridge " DVBRIDGE_VERSION "</h3>"
		"<p>DMR 마스터(BrandMeister 등) 접속 + DVSwitch Mobile(USRP) 브릿지, 다중 사용자</p>"
		"<p><b>최초 제작: 최승훈 (Choi Seunghun, 6K2KZR)</b></p>"
		"<p>MIT License — 누구나 자유롭게 사용·수정·배포할 수 있으며, "
		"모든 사본에 위 저작권 표시와 라이선스 문구를 유지해야 합니다.</p>"
		"<p>DMR 처리는 ETSI TS 102 361-1 표준을 근거로 새로 작성했습니다.<br>"
		"보코더(AMBE+2)는 들어 있지 않습니다. 사용자가 직접 구한 플러그인(라이선스·특허 조건은 각자 확인)을 plugins 폴더에 넣어 사용합니다.</p>"
		"<p>ID 명단은 RadioID(radioid.net)의 공개 명단을 사용자가 받아 씁니다.</p>"
		"<p>이 프로그램은 Qt(LGPL v3)를 사용합니다. Qt 라이브러리는 별도 DLL로 들어 있어 교체할 수 있습니다.</p>"
		"<p>아마추어 무선 자격이 있는 사람만 사용하세요.</p>");
}

QStringList MainWindow::vocoder_plugins() const
{
	QStringList dlls = QDir(app_.effective_plugin_dir()).entryList(QStringList() << "vocoder_*.dll", QDir::Files);
	dlls.erase(std::remove_if(dlls.begin(), dlls.end(), [](const QString &f) { return f.startsWith("vocoder_dummy", Qt::CaseInsensitive); }), dlls.end());
	return dlls;
}

// 보코더 플러그인(시험용 더미 제외)이 없으면 경고: 음성 없이 무음으로만 동작하므로
void MainWindow::check_vocoder(bool popup)
{
	const QString dir = app_.effective_plugin_dir();
	const QStringList dlls = vocoder_plugins();
	if (!dlls.isEmpty()) { append_log("보코더 플러그인: " + dlls.join(", ")); return; }
	append_log("경고: 보코더 플러그인이 없습니다 - 음성 없이(무음) 동작합니다. 폴더: " + QDir::toNativeSeparators(dir));
	if (!popup) { if (tray_) tray_->showMessage("DVBridge", "보코더 플러그인이 없어 음성이 나오지 않습니다.", QSystemTrayIcon::Warning); return; }
	// 창을 막지 않는 안내 (열려 있어도 서비스 자동 시작·업데이트 종료가 멈추지 않게)
	auto *box = new QMessageBox(QMessageBox::Warning, "보코더 플러그인 없음",
		"보코더 플러그인이 없어 DMR 음성을 압축·복원할 수 없습니다. "
		"이대로 접속하면 송신은 무음, 수신 음성은 들리지 않습니다.\n\n"
		"DVBridge에는 보코더가 들어 있지 않습니다. AMBE+2 보코더 플러그인(vocoder_*.dll)은 "
		"사용하시는 분이 직접 구하시고, 라이선스·특허 조건도 직접 확인해 주세요.\n\n"
		"구한 파일을 아래 폴더에 넣고 DVBridge를 다시 시작하면 됩니다.\n\n"
		+ QDir::toNativeSeparators(dir), QMessageBox::Ok, this);
	box->setAttribute(Qt::WA_DeleteOnClose);
	box->setWindowModality(Qt::NonModal);
	QPushButton *open = box->addButton("plugins 폴더 열기", QMessageBox::ActionRole);
	connect(box, &QMessageBox::buttonClicked, this, [dir, open](QAbstractButton *b) {
		if (b != open) return;
		QDir().mkpath(dir);
		QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
	});
	box->show();
}

bool MainWindow::autostart_enabled()
{
	QSettings s(kRunKey, QSettings::NativeFormat);
	// 예전 이름 시절의 자동 실행 항목 → 새 실행 파일로 바꿈. 같은 폴더에 설치된 것일 때만 (다른 복사본이 가져가지 않게)
	const QString old = s.value("DMRBridge").toString();
	const QString here = QDir::toNativeSeparators(QCoreApplication::applicationDirPath()) + "\\";
	if (!old.isEmpty() && old.contains(here, Qt::CaseInsensitive)) {
		s.remove("DMRBridge");
		set_autostart(true);
	}
	return QSettings(kRunKey, QSettings::NativeFormat).contains("DVBridge");
}

void MainWindow::set_autostart(bool on)
{
	QSettings s(kRunKey, QSettings::NativeFormat);
	if (on) s.setValue("DVBridge", "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\" --tray");
	else s.remove("DVBridge");
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	if (!quitting_) {	// 창을 닫으면 트레이로 (서비스는 계속)
		hide();
		if (!tray_notice_) {
			tray_notice_ = true;
			tray_->showMessage("DVBridge", "DVBridge는 계속 실행 중입니다. 작업 표시줄 오른쪽 아이콘에서 열거나 종료할 수 있습니다.");
		}
		e->ignore();
		return;
	}
	stop_all();
	e->accept();
}

void MainWindow::quit_app()
{
	if (service_on_) {
		showNormal(); raise(); activateWindow();
		if (QMessageBox::question(this, "DVBridge", "종료하면 무전 연결이 모두 끊깁니다.\n종료할까요?") != QMessageBox::Yes) return;
	}
	quit_now();
}

QString MainWindow::resume_flag() { return QDir(AppSettings::config_dir()).filePath("resume_service"); }

void MainWindow::quit_now(bool for_update)
{
	if (for_update && service_on_) {		// 업데이트·음성 팩 설치 뒤 다시 켜질 때 서비스를 원래대로 (자동 시작이 꺼져 있어도)
		QFile f(resume_flag());
		if (f.open(QIODevice::WriteOnly)) f.write("1");
	}
	quitting_ = true;
	// 열려 있는 안내 창·대화 상자를 먼저 닫음 (모달 창이 열려 있으면 종료가 그 창에서 멈춤)
	for (QWidget *w : QApplication::topLevelWidgets())
		if (w != this && w->isVisible() && qobject_cast<QDialog *>(w)) w->close();
	stop_all();
	append_log("종료");
	QFile f(clean_exit_flag());
	if (f.open(QIODevice::WriteOnly)) f.write("1");
	if (tray_) tray_->hide();
	QApplication::quit();
	if (for_update) QTimer::singleShot(5000, [] { std::_Exit(0); });	// 그래도 안 끝나면 (설치 파일이 기다림) 강제로 끝냄 - 정상 종료 표시는 이미 씀
}

// ===================== 서비스 =====================

bool MainWindow::set_service(bool on, QString *err)
{
	if (!on) {
		service_on_ = false;
		AppSettings::save_service_state(false);
		stop_all();
		append_log("서비스 끔");
		refresh_status();
		return true;
	}
	const QStringList problems = AppSettings::validate(app_);
	auto fail = [&](const QString &m) { append_log("서비스를 켤 수 없음: " + m); if (err) *err = m; refresh_status(); return false; };
	if (app_.users.isEmpty()) return fail("사용자가 없습니다. [웹 설정 열기]에서 사용자를 추가하세요.");
	if (!problems.isEmpty()) return fail("설정을 확인하세요:\n" + problems.join("\n"));
	int n = 0;
	for (int i = 0; i < app_.users.size(); ++i) if (app_.users[i].enabled) { start_user(i); ++n; }
	if (!n) return fail("'사용'이 켜진 사용자가 없습니다.");
	service_on_ = true;
	AppSettings::save_service_state(true);
	append_log("서비스 켬");
	refresh_status();
	return true;
}

void MainWindow::start_user(int i)
{
	Row &row = rows_[size_t(i)];
	if (row.session) return;
	SessionConfig c = app_.users[i];
	// 같은 콜사인을 여러 핫스팟으로 쓸 수 있으므로 핫스팟 접미사까지 붙여 구분 (이력의 '사용자', 대시보드 카드)
	const int sfx = AppSettings::hotspot_suffix(c);
	c.name = c.master.callsign + (sfx ? QString("·%1").arg(sfx, 2, 10, QChar('0')) : QString());
	c.plugin_dir = app_.effective_plugin_dir();
	c.intercom_tg = app_.intercom_tg;
	c.echo_tg = app_.echo_tg;
	c.rx_target_only = app_.rx_target_only;
	row.session = std::make_unique<Session>(c);
	Session *s = row.session.get();
	const QString tag = "[" + c.master.callsign + "·" + (sfx ? QString("%1").arg(sfx, 2, 10, QChar('0')) : QString::number(c.master.id)) + "] ";
	if (c.master.host.contains("brandmeister", Qt::CaseInsensitive) && c.master.port != 62031)
		append_log(tag + QString("주의: BrandMeister 마스터 포트는 보통 62031입니다 (지금 %1). 여러 사용자도 모두 62031로 같아도 됩니다").arg(c.master.port));
	connect(s, &Session::log, this, [this, tag](QString l) { append_log(tag + l); });
	connect(s, &Session::call_finished, calls_, &CallLog::add);
	// 내부 채널: 이 사용자가 말한 소리를 같은 내부 채널에 있는 다른 사용자에게 (BM과 무관)
	connect(s, &Session::intercom_audio, this, [this, s](QVector<int16_t> pcm, bool end) {
		const SessionConfig &me = s->config();
		const QString key = QString::number(me.master.id);
		for (Row &r : rows_) {
			Session *o = r.session.get();
			if (o && o != s) o->intercom_feed(key, me.dmr_id, me.master.callsign, me.name, pcm, end);
		}
	});
	connect(s, &Session::audio, this, [this, i](QByteArray pcm, bool) { web_->push_audio(i, pcm); });	// 실시간 듣기
	s->set_id_db(&ids_);
	connect(s->master(), &HomebrewClient::state_changed, this, [this, i, s] {
		if (size_t(i) < rows_.size() && rows_[size_t(i)].session.get() == s) { rows_[size_t(i)].master_state = s->master_state_text(); refresh_row(i); }
	});
	connect(s->usrp(), &UsrpPort::client_changed, this, [this, i, s, tag](QString a) {
		if (size_t(i) < rows_.size() && rows_[size_t(i)].session.get() == s) {
			const QString kind = NetKind::of_text(a);
			rows_[size_t(i)].client = a.isEmpty() ? QString("없음") : a + (kind.isEmpty() ? QString() : " (" + kind + ")");
			if (!a.isEmpty()) append_log(tag + QString("휴대폰 연결: %1 — %2").arg(a, kind));
			refresh_row(i);
		}
	});
	connect(s, &Session::target_changed, this, [this, i] { refresh_row(i); });
	connect(s, &Session::device_changed, this, [this, i, s](QString d) {	// 장비명 저장 → 다음 시작 때 BM 위치 칸에 바로 사용
		if (size_t(i) >= rows_.size() || rows_[size_t(i)].session.get() != s || i >= app_.users.size()) return;
		app_.users[i].device = d;
		AppSettings::save(app_);
	});
	connect(s, &Session::tx_state, this, [this, i, s](bool on, int tg, bool priv) {
		if (size_t(i) >= rows_.size() || rows_[size_t(i)].session.get() != s) return;
		rows_[size_t(i)].air = on ? QString("%1 → %2%3").arg(tg == s->config().unlink_tg ? "Unlink" : "송신").arg(tg).arg(priv ? " (개인)" : "") : QString();
		refresh_row(i);
	});
	connect(s, &Session::rx_state, this, [this, i, s](bool on, quint32 src, quint32 dst, bool priv) {
		if (size_t(i) >= rows_.size() || rows_[size_t(i)].session.get() != s) return;
		rows_[size_t(i)].air = on ? QString("수신 %1 → %2%3").arg(ids_.display(src)).arg(dst).arg(priv ? " (개인)" : "") : QString();
		refresh_row(i);
	});
	if (!s->start()) {
		append_log(tag + "시작 실패 (USRP 포트가 이미 사용 중인지 확인하세요)");
		row.session.reset();
	}
	refresh_row(i);
}

void MainWindow::stop_all()
{
	bool any = false;
	for (size_t i = 0; i < rows_.size(); ++i) {
		Row &r = rows_[i];
		if (!r.session) continue;
		r.session->stop();
		any = true;
	}
	if (any) QApplication::processEvents();	// RPTCL 전송
	for (size_t i = 0; i < rows_.size(); ++i) {
		rows_[i].session.reset();
		rows_[i].master_state = "정지";
		rows_[i].client = "없음";
		rows_[i].air.clear();
	}
	if (any) append_log("전체 정지");
	if (table_) refresh_table();
}

bool MainWindow::running() const
{
	for (const Row &r : rows_) if (r.session) return true;
	return false;
}

QString MainWindow::apply_user(int i, const SessionConfig &c)
{
	if (i < 0 || i >= app_.users.size()) {
		app_.users.append(c);
		rows_.resize(app_.users.size());
		AppSettings::save(app_);
		append_log("사용자 추가: " + c.master.callsign);
		if (service_on_ && c.enabled && AppSettings::validate(app_).isEmpty()) start_user(app_.users.size() - 1);
	} else {
		const bool was_running = bool(rows_[size_t(i)].session);
		if (was_running) {
			rows_[size_t(i)].session->stop();
			QApplication::processEvents();
			rows_[size_t(i)].session.reset();
			rows_[size_t(i)].master_state = "정지"; rows_[size_t(i)].client = "없음"; rows_[size_t(i)].air.clear();
		}
		app_.users[i] = c;
		AppSettings::save(app_);
		append_log("사용자 설정 저장: " + c.master.callsign);
		if (service_on_ && c.enabled) {
			const QStringList problems = AppSettings::validate(app_);
			if (problems.isEmpty()) start_user(i);
			else append_log("다시 시작 안 함: " + problems.join(" / "));
		}
	}
	refresh_status();
	return AppSettings::validate(app_).join(" / ");
}

void MainWindow::delete_user(int i)
{
	if (i < 0 || i >= app_.users.size()) return;
	const QString call = app_.users[i].master.callsign;
	const bool was_on = service_on_;
	stop_all();		// 행 번호가 바뀌므로 전체 정지 후 다시 시작
	rows_.erase(rows_.begin() + i);
	app_.users.removeAt(i);
	AppSettings::save(app_);
	append_log("사용자 삭제: " + call);
	if (was_on) set_service(!app_.users.isEmpty());
	refresh_status();
}

// 와치독 (프로그램 안): 서비스가 켜져 있으면 꺼진 접속을 다시 켜고, 웹 화면이 닫혔으면 다시 엶
void MainWindow::watchdog_tick()
{
	if (quitting_ || !app_.watchdog) return;		// 끝내는 중에는 다시 켜지 않음
	if (!web_->listening()) { append_log("와치독: 웹 화면 다시 시작"); start_dashboard(); }
	if (!service_on_) return;
	if (!AppSettings::validate(app_).isEmpty()) return;
	for (int i = 0; i < app_.users.size(); ++i) {
		if (!app_.users[i].enabled || rows_[size_t(i)].session) continue;
		append_log("와치독: " + app_.users[i].master.callsign + " 접속 다시 시작");
		start_user(i);
	}
}

// 와치독 (프로그램 밖): 같은 실행 파일을 --watchdog 으로 띄워 이 프로세스를 지켜봄. 비정상 종료면 다시 실행
void MainWindow::spawn_watchdog()
{
	static bool spawned = false;
	if (spawned) return;
	spawned = QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--watchdog", QString::number(QCoreApplication::applicationPid())});
	if (!spawned) append_log("와치독 감시 프로세스를 시작하지 못했습니다");
}

// ===================== 웹 =====================

void MainWindow::start_dashboard()
{
	web_->stop();
	web_ok_ = web_->start(QHostAddress(app_.web_bind), app_.web_port);
	if (web_ok_) append_log("웹 화면: " + local_url() + (app_.web_bind != "127.0.0.1" ? QString("  (다른 기기: %1)").arg(access_urls(QString()).join(", ")) : QString()));
	else append_log(QString("웹 화면 시작 실패 - %1:%2 를 열 수 없습니다 (포트 사용 중?)").arg(app_.web_bind).arg(app_.web_port));
	refresh_status();
}

QString MainWindow::local_url(const QString &path) const
{
	// 특정 IP에만 열었으면 localhost로는 안 열리므로 그 IP 사용
	const QString host = (app_.web_bind == "0.0.0.0" || app_.web_bind == "127.0.0.1") ? QString("localhost") : app_.web_bind;
	return QString("http://%1:%2/%3").arg(host).arg(app_.web_port).arg(path);
}

QStringList MainWindow::access_urls(const QString &path) const
{
	QStringList u;
	if (app_.web_bind == "127.0.0.1") return u;
	const QStringList ips = app_.web_bind == "0.0.0.0" ? lan_ipv4() : QStringList{app_.web_bind};
	for (const QString &ip : ips) u << QString("http://%1:%2/%3").arg(ip).arg(app_.web_port).arg(path);
	return u;
}

QJsonObject MainWindow::web_config() const
{
	QJsonObject o;
	QJsonArray users;
	for (int i = 0; i < app_.users.size(); ++i) {
		QJsonObject u = AppSettings::user_to_json(app_.users[i]);
		const Row &r = rows_[size_t(i)];
		u["running"] = bool(r.session);			// 지금 접속 중이면 현재 대상(휴대폰·웹에서 바꾼 값)
		u["connected"] = r.session && r.session->master()->connected();
		u["live_tg"] = r.session ? r.session->config().tg : app_.users[i].tg;
		u["live_priv"] = r.session ? r.session->config().priv : app_.users[i].priv;
		u["client"] = r.session ? r.session->client_address() : QString();
		u["client_net"] = NetKind::of_text(u["client"].toString());
		u["key"] = QString::number(app_.users[i].master.id);		// 공지 대상 구분 (핫스팟 ID)
		u["notice_on"] = r.session && r.session->notice_active();
		u["notice_wait"] = r.session ? r.session->notice_waiting() : 0;
		if (r.session) u["notice_status"] = r.session->notice_status();
		users.append(u);
	}
	o["users"] = users;
	o["service_on"] = service_on_;
	o["defaults"] = AppSettings::user_to_json(AppSettings::default_user());
	quint16 port = 32001;	// 겹치지 않는 USRP 포트 제안
	for (bool clash = true; clash; ) {
		clash = false;
		for (const SessionConfig &u : app_.users) if (u.usrp_port == port) { ++port; clash = true; }
	}
	o["next_usrp_port"] = int(port);
	o["vocoders"] = QJsonArray::fromStringList(vocoder_plugins());
	o["plugin_dir"] = QDir::toNativeSeparators(app_.effective_plugin_dir());
	o["iddb_count"] = ids_.size();
	if (ids_.size()) o["iddb_date"] = ids_.file_time().toString("yyyy-MM-dd");
	o["iddb_busy"] = id_updater_ && id_updater_->busy();
	o["iddb_status"] = id_status_;
	o["version"] = DVBRIDGE_VERSION;
	o["intercom_tg"] = app_.intercom_tg;
	o["echo_tg"] = app_.echo_tg;
	o["rx_target_only"] = app_.rx_target_only;
	if (netcheck_) o["netcheck"] = netcheck_->result();
	QJsonObject n = NoticeService::sanitize(app_.notice, nullptr);
	if (n.isEmpty()) n = NoticeService::sanitize(QJsonObject(), nullptr);
	o["notice"] = QJsonObject{{"config", n}, {"voices", notice_->voices()}, {"tts_ok", notice_->tts_available()},
							  {"making", notice_->pending()}, {"recent", notice_->recent()}, {"wav_id", notice_wav_id_},
							  {"voicepack", notice_->voicepack()}, {"job", notice_->status()}};
	return o;
}

QJsonObject MainWindow::web_action(const QString &action, const QJsonObject &body)
{
	auto result = [](bool ok, const QString &m) { return QJsonObject{{"ok", ok}, {"message", m}}; };
	if (action.startsWith("notice_")) return notice_action(action, body);
	if (action == "intercom_save") {		// 내부 채널·내부 에코 번호 (0 = 끔)
		const int tg = body["tg"].toInt(-1);
		const int echo = body.contains("echo_tg") ? body["echo_tg"].toInt(-1) : app_.echo_tg;
		auto reserved = [](int n) { return n == 4000 || n == 9990 || (n >= 100997 && n <= 999997 && n % 1000 == 997); };
		if (tg < 0 || tg > 999999 || echo < 0 || echo > 999999) return result(false, "번호를 확인하세요 (0이면 끔)");
		if (reserved(tg) || reserved(echo)) return result(false, "4000(Unlink)·BM 에코(9990, 450997 등) 번호는 쓸 수 없습니다");
		if (tg && tg == echo) return result(false, "내부 채널과 내부 에코는 다른 번호여야 합니다");
		app_.intercom_tg = tg;
		app_.echo_tg = echo;
		AppSettings::save(app_);
		for (Row &r : rows_) if (r.session) { r.session->set_intercom_tg(tg); r.session->set_echo_tg(echo); }
		append_log(QString("(웹 설정) 내부 채널 %1 · 내부 에코 %2").arg(tg ? QString::number(tg) : QString("끔"), echo ? QString::number(echo) : QString("끔")));
		QStringList m;
		m << (tg ? QString("내부 채널 %1#").arg(tg) : QString("내부 채널 끔"));
		m << (echo ? QString("내부 에코 %1#").arg(echo) : QString("내부 에코 끔"));
		return result(true, "저장했습니다: " + m.join(", "));
	}
	if (action == "rx_filter_save") {	// 지금 채널만 듣기 켜기/끄기
		app_.rx_target_only = body["on"].toBool(true);
		AppSettings::save(app_);
		for (Row &r : rows_) if (r.session) r.session->set_rx_target_only(app_.rx_target_only);
		append_log(QString("(웹 설정) 지금 채널만 듣기: %1").arg(app_.rx_target_only ? "켬" : "끔"));
		return result(true, app_.rx_target_only ? "지금 채널만 듣습니다 (다른 TG 소리는 휴대폰으로 보내지 않음)" : "BM이 보내 주는 모든 TG 소리를 휴대폰으로 보냅니다");
	}
	if (action == "test_sound") {		// 그 사용자의 휴대폰에만 시험 소리 (화면 이름표 + 소리 확인, BM과 무관)
		const int i = body["index"].toInt(-1);
		if (i < 0 || i >= app_.users.size()) return result(false, "없는 사용자입니다");
		Row &r = rows_[size_t(i)];
		if (!r.session) return result(false, "이 사용자가 켜져 있지 않습니다. 서비스를 켜세요");
		if (r.session->client_address().isEmpty()) return result(false, "이 사용자에게 연결된 휴대폰이 없습니다");
		const QString hid = QString::number(app_.users[i].master.id);
		append_log("(웹 설정) 시험 소리: " + app_.users[i].master.callsign);
		if (notice_->tts_available()) {
			NoticeService::Job j;
			j.text = QStringLiteral("디브이브릿지 시험 소리입니다. 화면과 소리가 잘 나오면 정상입니다.");
			j.source = "test";
			j.targets = QStringList{hid};
			j.chime = true;
			notice_->speak(j);
			return result(true, "시험 소리를 만들고 있습니다. 곧 그 휴대폰에서 딩동 소리와 안내가 나옵니다");
		}
		QVector<int16_t> beep(8000);		// 이 PC에 음성 합성 목소리가 없으면 딩동 + 1초 삐 소리
		for (int k = 0; k < beep.size(); ++k) beep[k] = int16_t(4000 * std::sin(2 * 3.14159265358979 * 1000 * k / 8000.0));
		notice_dispatch(NoticeService::build(beep, true, 1), QStringLiteral("시험 소리"), "test", QStringList{hid});
		return result(true, "시험 소리(딩동 + 삐)를 보냈습니다");
	}
	if (action == "save_user") {
		const int i = body["index"].toInt(-1);
		const bool edit = i >= 0 && i < app_.users.size();
		SessionConfig c = edit ? app_.users[i] : AppSettings::default_user();
		QString err;
		if (!AppSettings::user_from_json(body["user"].toObject(), c, &err)) return result(false, err);
		const QString warn = apply_user(edit ? i : -1, c);
		append_log("(웹 설정) " + QString(edit ? "편집: " : "추가: ") + c.master.callsign);
		return result(true, warn.isEmpty() ? "저장했습니다" : "저장했습니다. 서비스를 켜기 전에 확인하세요: " + warn);
	}
	if (action == "delete_user") {
		const int i = body["index"].toInt(-1);
		if (i < 0 || i >= app_.users.size()) return result(false, "없는 사용자입니다");
		const QString call = app_.users[i].master.callsign;
		delete_user(i);
		append_log("(웹 설정) 삭제: " + call);
		return result(true, call + " 사용자를 삭제했습니다");
	}
	if (action == "start_all") {
		QString err;
		const bool ok = set_service(true, &err);
		return result(ok, ok ? "서비스를 켰습니다" : err);
	}
	if (action == "set_tg") {	// 사용자별 TG 직접 바꾸기 (휴대폰 번호# 입력과 같은 동작)
		const int i = body["index"].toInt(-1);
		if (i < 0 || i >= app_.users.size()) return result(false, "없는 사용자입니다");
		const int tg = body["tg"].toInt(0);
		const bool priv = body["priv"].toBool(false);
		const bool save = body["save_default"].toBool(false);
		if (tg < 1 || tg > 16777215) return result(false, "TG 번호를 확인하세요");
		SessionConfig &c = app_.users[i];
		Row &r = rows_[size_t(i)];
		QString live;
		if (r.session) {
			const QString err = r.session->set_target(tg, priv, "웹 설정");
			if (!err.isEmpty()) return result(false, err);
			refresh_row(i);
			live = tg == c.unlink_tg && !priv ? QString("Unlink(4000)를 보냈습니다") : QString("%1 → %2%3 로 바꿨습니다").arg(c.master.callsign).arg(tg).arg(r.session->config().priv ? " (개인)" : "");
		}
		const bool is_unlink = tg == c.unlink_tg && !priv;
		if ((save || !r.session) && !is_unlink) {
			c.tg = r.session ? r.session->config().tg : tg;
			c.priv = r.session ? r.session->config().priv : priv;
			AppSettings::save(app_);
			append_log(QString("(웹 설정) %1 기본 대상 저장: %2%3").arg(c.master.callsign).arg(c.tg).arg(c.priv ? " 개인" : ""));
		}
		if (!r.session) return result(!is_unlink, is_unlink ? "접속 중이 아니라 Unlink를 보낼 수 없습니다" : QString("접속 중이 아니라 기본 대상(%1)으로 저장했습니다 — 서비스를 켜면 이 TG로 시작").arg(tg));
		return result(true, live + (save && !is_unlink ? " (기본값으로도 저장)" : ""));
	}
	if (action == "netcheck") {		// 내부망·외부망 점검 (읽기만, 결과는 config.netcheck)
		if (!netcheck_) {
			netcheck_ = new NetCheck(this);
			connect(netcheck_, &NetCheck::finished, this, [this] { append_log("네트워크 점검 끝"); });
		}
		if (netcheck_->running()) return result(true, "점검 중입니다");
		NetCheck::Input in;
		in.web_bind = app_.web_bind;
		in.web_port = app_.web_port;
		in.web_listening = web_->listening();
		in.admin_password = AppSettings::has_admin_password(app_);
		in.exe_path = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
		QStringList hosts;
		for (int i = 0; i < app_.users.size(); ++i) {
			const SessionConfig &c = app_.users[i];
			if (!hosts.contains(c.master.host)) hosts << c.master.host;
			const Row &r = rows_[size_t(i)];
			const int sfx = AppSettings::hotspot_suffix(c);
			in.users.append(QJsonObject{{"call", c.master.callsign + (sfx ? QString("·%1").arg(sfx, 2, 10, QChar('0')) : QString())},
										{"usrp_port", int(c.usrp_port)}, {"running", bool(r.session)},
										{"connected", r.session && r.session->master()->connected()},
										{"client", r.session ? r.session->client_address() : QString()}});
		}
		in.master_hosts = hosts;
		const bool pub = body["public_ip"].toBool(false);
		netcheck_->start(in, pub);
		append_log(pub ? "네트워크 점검 시작 (공인 IP 조회 포함: api.ipify.org)" : "네트워크 점검 시작");
		return result(true, "점검을 시작했습니다");
	}
	if (action == "stop_all") {
		append_log("(웹 설정) 서비스 끄기");
		set_service(false);
		return result(true, "서비스를 껐습니다");
	}
	if (action == "download_ids") {
		if (id_updater_ && id_updater_->busy()) return result(true, "이미 받는 중입니다");
		download_id_db();
		return result(true, "RadioID 명단을 받기 시작했습니다 (약 17MB)");
	}
	if (action == "restore") {
		const QByteArray ini = body["ini"].toString().toUtf8();
		if (ini.isEmpty()) return result(false, "백업 파일 내용이 없습니다");
		const QString tmp = QDir(AppSettings::backup_dir()).filePath("uploaded-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".ini");
		{
			QFile f(tmp);
			if (!f.open(QIODevice::WriteOnly)) return result(false, "임시 파일을 쓸 수 없습니다");
			f.write(ini);
		}
		int n = 0;
		QString err;
		if (!AppSettings::check_backup(tmp, &n, &err)) { QFile::remove(tmp); return result(false, err); }
		set_service(false);
		if (!AppSettings::restore_from(tmp, &err)) return result(false, err);
		app_ = AppSettings::load();
		notice_->set_config(app_.notice);
		rows_.clear();
		rows_.resize(app_.users.size());
		int nopw = 0;
		for (const SessionConfig &c : app_.users) nopw += c.master.password.isEmpty();
		append_log(QString("(웹 설정) 백업 복원 (사용자 %1명)").arg(app_.users.size()));
		refresh_status();
		QString m = QString("복원했습니다 (사용자 %1명). 서비스는 꺼 두었습니다 — 확인 후 [서비스 켜기]").arg(app_.users.size());
		if (nopw) m += QString(". 비밀번호를 읽지 못한 사용자 %1명은 편집에서 핫스팟 비밀번호를 다시 넣어 주세요 (다른 PC에서 만든 백업)").arg(nopw);
		return result(true, m);
	}
	return result(false, "지원하지 않는 요청");
}

// ===================== 브릿지 공지 =====================

int MainWindow::notice_dispatch(const QVector<int16_t> &pcm, const QString &title, const QString &source, const QStringList &targets, bool ai)
{
	int n = 0;
	for (size_t i = 0; i < rows_.size() && int(i) < app_.users.size(); ++i) {
		Session *s = rows_[i].session.get();
		if (!s) continue;
		if (!targets.isEmpty() && !targets.contains(QString::number(app_.users[int(i)].master.id))) continue;
		if (source == "live") s->notice_live(pcm, title == "end");
		else s->notice_add(title, source, pcm, ai);
		++n;
	}
	return n;
}

QJsonObject MainWindow::notice_action(const QString &action, const QJsonObject &body)
{
	auto result = [](bool ok, const QString &m) { return QJsonObject{{"ok", ok}, {"message", m}}; };
	QStringList targets;
	for (const QJsonValue &v : body["targets"].toArray()) targets << v.toString();
	auto running_targets = [&] {
		int n = 0;
		for (size_t i = 0; i < rows_.size() && int(i) < app_.users.size(); ++i)
			if (rows_[i].session && (targets.isEmpty() || targets.contains(QString::number(app_.users[int(i)].master.id)))) ++n;
		return n;
	};
	if (action == "notice_say") {		// 문장 읽기 공지 (지금)
		const QString text = body["text"].toString().trimmed();
		if (text.isEmpty()) return result(false, "안내할 문장을 넣어 주세요");
		if (text.size() > 500) return result(false, "안내 문장은 500자까지입니다");
		if (!notice_->tts_available()) return result(false, "이 PC에 음성 합성 목소리가 없어 문장 읽기를 할 수 없습니다 (마이크 방송은 됩니다)");
		if (!running_targets()) return result(false, "보낼 사용자가 없습니다 — 서비스를 켜고 대상 사용자가 접속 중인지 확인하세요");
		NoticeService::Job j;
		j.text = text;
		j.targets = targets;
		j.chime = body["chime"].toBool(true);
		j.repeat = body["repeat"].toInt(1);
		notice_->speak(j);
		append_log("(웹 설정) 공지 문장 읽기: " + text.left(40));
		return result(true, "공지를 만들고 있습니다. 통화가 없을 때 바로 나갑니다");
	}
	if (action == "notice_preview") {	// 미리 듣기: 음성만 만들고 휴대폰에는 안 보냄 (웹이 /api/admin/notice.wav 로 받아 PC 스피커로 재생)
		const QString text = body["text"].toString().trimmed();
		if (text.isEmpty()) return result(false, "미리 들을 문장을 넣어 주세요");
		if (text.size() > 500) return result(false, "안내 문장은 500자까지입니다");
		if (!notice_->tts_available()) return result(false, "이 PC에 음성 합성 목소리가 없어 문장 읽기를 할 수 없습니다");
		NoticeService::Job j;
		j.text = text;
		j.source = "preview";
		j.chime = body["chime"].toBool(true);
		j.repeat = 1;
		notice_->speak(j);
		return result(true, "미리 듣기 음성을 만들고 있습니다");
	}
	if (action == "notice_live") {		// 실시간 마이크 조각 (8kHz 16bit 모노, Base64)
		const QByteArray raw = QByteArray::fromBase64(body["pcm"].toString().toLatin1());
		QVector<int16_t> pcm(raw.size() / 2);
		for (int i = 0; i < pcm.size(); ++i) pcm[i] = int16_t(quint16(uchar(raw[i * 2])) | quint16(uchar(raw[i * 2 + 1])) << 8);
		const bool end = body["end"].toBool(false);
		const int n = notice_dispatch(pcm, end ? "end" : QString(), "live", targets);
		if (!n) return result(false, "보낼 사용자가 없습니다 — 서비스를 켜고 대상 사용자가 접속 중인지 확인하세요");
		return result(true, end ? "방송을 마쳤습니다" : QString());
	}
	if (action == "notice_stop") {
		for (Row &r : rows_) if (r.session) r.session->notice_stop();
		append_log("(웹 설정) 공지 모두 멈춤");
		return result(true, "공지를 멈췄습니다");
	}
	if (action == "notice_save") {		// 목소리·빠르기·알림음·예약 저장
		QString err;
		const QJsonObject n = NoticeService::sanitize(body["notice"].toObject(), &err);
		if (n.isEmpty()) return result(false, err.isEmpty() ? "설정을 확인하세요" : err);
		app_.notice = n;
		AppSettings::save(app_);
		notice_->set_config(n);
		append_log(QString("(웹 설정) 공지 설정 저장 (예약 %1개)").arg(n["schedule"].toArray().size()));
		if (n["voice"].toString().startsWith("st:") && !n["ai_agree"].toBool())
			return result(true, "저장했습니다. 자연스러운 목소리는 사용 제한에 동의해야 쓸 수 있어 지금은 Windows 목소리로 읽습니다");
		return result(true, "공지 설정을 저장했습니다");
	}
	return result(false, "지원하지 않는 요청");
}

// ===================== ID 명단 =====================

QString MainWindow::id_db_path() const
{
	return QDir(AppSettings::config_dir()).filePath("user.csv");
}

void MainWindow::load_id_db()
{
	if (!QFile::exists(id_db_path())) { id_status_ = "명단 없음 — [명단 받기]를 누르면 콜사인·이름으로 표시"; return; }
	if (!ids_.load(id_db_path())) { id_status_ = "명단 파일을 읽지 못함 — 다시 받아 주세요"; return; }
	const qint64 days = ids_.file_time().daysTo(QDateTime::currentDateTime());
	id_status_ = days >= 30 ? QString("받은 지 %1일 지남 — 새로 받을 수 있습니다").arg(days) : QString();
	append_log(QString("ID 명단 %L1명 불러옴").arg(ids_.size()));
}

void MainWindow::download_id_db()
{
	if (!id_updater_) {
		id_updater_ = new IdDbUpdater(this);
		connect(id_updater_, &IdDbUpdater::progress, this, [this](qint64 got, qint64 total) {
			id_status_ = total > 0 ? QString("받는 중 %1%").arg(got * 100 / total) : QString("받는 중 %1MB").arg(got / 1048576);
		});
		connect(id_updater_, &IdDbUpdater::finished, this, [this](bool ok, QString msg) {
			append_log(msg);
			id_status_ = msg;
			if (ok) load_id_db();
		});
	}
	if (id_updater_->busy()) return;
	append_log(QString("RadioID 명단 받기 시작: %1").arg(IdDbUpdater::kDefaultUrl));
	id_status_ = "받기 시작";
	id_updater_->start(id_db_path());
}
