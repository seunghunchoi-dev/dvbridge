// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
// 웹 설정 페이지 (외부 라이브러리·인터넷 없이 동작, 밝은/어두운 테마, 휴대폰 화면 대응)
//   관리자 로그인 후: 서비스 켜기/끄기, 브릿지 공지(문장 읽기·마이크·예약), 사용자 추가·편집·삭제, 보코더 상태, ID 명단 받기, 백업·복원, 관리자 비밀번호 변경
//   핫스팟 비밀번호는 서버가 내보내지 않음 — 입력 칸은 바꿀 때만 씀
#include "dashboard_server.h"

QByteArray DashboardServer::settings_html()
{
	return QByteArray(R"HTML(<!doctype html>
<html lang="ko"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DVBridge 설정</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#6b7280;--line:#e5e7eb;--tx:#c0392b;--ok:#1e8449;--acc:#d35400;--in:#fff;color-scheme:light}
:root[data-theme=dark]{--bg:#121417;--card:#1c1f24;--fg:#e8eaed;--mut:#9aa0a6;--line:#2c3037;--tx:#e74c3c;--ok:#27ae60;--acc:#e67e22;--in:#15181c;color-scheme:dark}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#121417;--card:#1c1f24;--fg:#e8eaed;--mut:#9aa0a6;--line:#2c3037;--tx:#e74c3c;--ok:#27ae60;--acc:#e67e22;--in:#15181c;color-scheme:dark}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,"Malgun Gothic",sans-serif}
header{background:#c0392b;color:#fff;padding:9px 16px;display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap}
header b{font-size:18px}header a{color:#fff;margin-left:10px;font-size:13px}
button.th,button.hd{border:1px solid rgba(255,255,255,.5);background:transparent;color:#fff;border-radius:6px;padding:2px 9px;cursor:pointer;font:inherit;font-size:12px;margin-left:10px}
main{max-width:1000px;margin:0 auto;padding:12px 16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:12px 14px;margin-bottom:12px}
h2{font-size:15px;margin:0 0 8px}
.tbl{overflow-x:auto}table{width:100%;border-collapse:collapse}th,td{padding:7px 8px;border-bottom:1px solid var(--line);text-align:left;white-space:nowrap}
th{font-size:12px;color:var(--mut)}tr:last-child td{border-bottom:0}
button{border:1px solid var(--line);background:var(--bg);color:var(--fg);border-radius:6px;padding:6px 12px;cursor:pointer;font:inherit}
button.pri{background:#c0392b;border-color:#c0392b;color:#fff}button.danger{color:var(--tx)}button.sm{padding:3px 9px;font-size:13px}
button:disabled{opacity:.5;cursor:default}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:10px 14px}
label{display:block;font-size:12px;color:var(--mut)}
input[type=text],input[type=number],input[type=password]{width:100%;padding:6px 8px;border:1px solid var(--line);border-radius:6px;background:var(--in);color:var(--fg);font:inherit;margin-top:2px}
.chk{display:flex;align-items:center;gap:6px;font-size:14px;color:var(--fg);margin-top:18px}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:10px}
.msg{min-height:18px;font-size:13px;margin-top:8px}.msg.err{color:var(--tx)}.msg.ok{color:var(--ok)}
.mut{color:var(--mut)}.hide{display:none}.tag{font-size:11px;border:1px solid var(--line);border-radius:4px;padding:0 4px;color:var(--mut)}
.box{max-width:380px;margin:30px auto}
.tgrow{display:flex;flex-wrap:wrap;gap:6px 10px;align-items:center;padding:8px 0;border-bottom:1px solid var(--line)}.tgrow:last-child{border-bottom:0}
.tgrow .who{min-width:120px;font-weight:600}.tgrow .now{min-width:170px;font-size:13px}
.tgrow input[type=number]{width:110px;margin:0}.qb{padding:3px 8px;font-size:12px}
.adv{list-style:none;padding:0;margin:8px 0 0}.adv li{padding:5px 0 5px 26px;position:relative;border-bottom:1px solid var(--line)}.adv li:last-child{border-bottom:0}
.adv li:before{position:absolute;left:2px;top:4px;font-weight:700}.adv li.ok:before{content:'✔';color:var(--ok)}.adv li.warn:before{content:'⚠';color:var(--acc)}.adv li.info:before{content:'ℹ';color:var(--mut)}
.net{font-size:11px;border-radius:4px;padding:0 5px;margin-left:4px;color:#fff;background:var(--mut)}.net.ext{background:#2471a3}.net.int{background:var(--ok)}
textarea,select{width:100%;padding:6px 8px;border:1px solid var(--line);border-radius:6px;background:var(--in);color:var(--fg);font:inherit;margin-top:2px}
.nsec{border-top:1px solid var(--line);margin-top:10px;padding-top:10px}.nsec h3{font-size:14px;margin:0 0 6px}
.who2{display:inline-flex;align-items:center;gap:4px;margin-right:12px;color:var(--fg);font-size:14px}.who2 select{width:auto;margin:0}
button.big{font-size:17px;padding:10px 22px}button.onair{background:var(--tx);border-color:var(--tx);color:#fff;animation:blink 1s infinite}
@keyframes blink{50%{opacity:.7}}
.meter{display:inline-block;width:160px;height:10px;border:1px solid var(--line);border-radius:5px;overflow:hidden;vertical-align:middle}.meter i{display:block;height:100%;width:0;background:var(--ok)}
.sch{display:flex;flex-wrap:wrap;gap:6px 10px;align-items:center;padding:8px 0;border-bottom:1px solid var(--line)}
.sch input[type=time]{padding:5px;border:1px solid var(--line);border-radius:6px;background:var(--in);color:var(--fg);font:inherit}
.sch .txt{flex:1 1 260px;margin:0}.sch select{width:auto;margin:0}
.day{display:inline-block;width:28px;text-align:center;border:1px solid var(--line);border-radius:5px;padding:2px 0;cursor:pointer;user-select:none;font-size:13px}.day.on{background:#c0392b;border-color:#c0392b;color:#fff}
</style></head><body>
<header><b>DVBridge 설정</b><span><a href="/">대시보드</a><button class="th" id="theme" onclick="cycleTheme()" title="화면 테마">테마</button><button class="hd hide" id="logout" onclick="logout()">로그아웃</button></span></header>
<main>
<div id="setup" class="card box hide"><h2>관리자 비밀번호 정하기</h2>
 <p class="mut" id="setupText">웹 설정을 쓰려면 먼저 관리자 비밀번호를 정하세요 (6자 이상).</p>
 <div id="setupForm"><label>새 비밀번호<input type="password" id="s1" autocomplete="new-password"></label>
 <label style="margin-top:8px">한 번 더<input type="password" id="s2" autocomplete="new-password"></label>
 <div class="row"><button class="pri" onclick="setup()">정하기</button></div></div>
 <div class="msg" id="setupMsg"></div></div>
<div id="login" class="card box hide"><h2>로그인</h2>
 <label>관리자 비밀번호<input type="password" id="lp" autocomplete="current-password" onkeydown="if(event.key==='Enter')login()"></label>
 <div class="row"><button class="pri" onclick="login()">로그인</button></div><div class="msg" id="loginMsg"></div></div>
<div id="app" class="hide">
 <div class="card"><div class="row" style="margin-top:0"><button class="pri" onclick="act('start_all')">서비스 켜기</button><button onclick="act('stop_all')">서비스 끄기</button><span class="mut" id="runState"></span></div><div class="msg" id="topMsg"></div></div>
 <div class="card"><h2>TG 바꾸기 (사용자별)</h2>
  <p class="mut" style="margin:0 0 8px">휴대폰에서 번호#을 누르는 것과 같습니다. 바꾸기 전에 자동으로 Unlink하고, 9990·450997(에코)은 개인호출로 바뀝니다.</p>
  <div id="tgs"></div>
  <label class="chk" style="margin-top:8px"><input type="checkbox" id="tgSave"> 기본값으로도 저장 (다음에 서비스를 켤 때 이 TG로 시작)</label>
  <div class="msg" id="tgMsg"></div>
  <div class="nsec"><h3>내부 채널·내부 에코 (BM으로 안 나감)</h3>
   <p class="mut" style="margin:0 0 6px">휴대폰에서 이 번호와 #을 누르면(예: 99#) 내부 채널로 들어갑니다. 내부 채널에서 말하면 BM으로는 나가지 않고, 이 브릿지에서 같은 내부 채널에 있는 사용자에게만 들립니다. BM으로 돌아갈 때는 TG 번호#(예: 450#).</p>
   <div class="row" style="margin-top:0"><label class="who2">내부 채널 번호 <input type="number" id="icTg" style="width:110px;margin:0" min="0"></label><label class="who2">내부 에코 번호 <input type="number" id="ecTg" style="width:110px;margin:0" min="0"></label><button onclick="icSave()">저장</button></div>
   <p class="mut" style="margin:4px 0 0;font-size:12px">내부 에코: 휴대폰에서 이 번호와 #을 누르고(예: 9999#) 말하면, BM으로 보내지 않고 DVBridge가 바로 내 목소리를 되돌려 들려줍니다 (마이크·스피커·연결 점검). 0이면 끔. 두 번호 모두 실제 BM TG와 겹치지 않는 번호를 쓰세요 (그 번호의 BM TG에는 갈 수 없게 됨)</p>
   <div class="msg" id="icMsg"></div></div>
  <div class="nsec"><h3>지금 채널만 듣기</h3>
   <label class="chk"><input type="checkbox" id="rxOnly" onchange="rxSave()"> 지금 채널과 다른 그룹 TG 소리는 휴대폰으로 보내지 않음 (권장)</label>
   <p class="mut" style="margin:4px 0 0;font-size:12px">BM은 전에 말했던 TG나 SelfCare에 고정한 TG의 소리를 계속 보내 줍니다. 켜 두면 지금 고른 채널 소리만 들립니다. 내 ID로 오는 개인호출, 공지, 내부 채널, 에코는 항상 들립니다. 채널을 바꾸면 예전 TG 연결도 자동으로 끊습니다(Unlink).</p>
   <div class="msg" id="rxMsg"></div></div></div>
 <div class="card" id="noticeCard"><h2>브릿지 공지 (휴대폰에 음성 안내)</h2>
  <p class="mut" style="margin:0 0 6px">BM으로는 나가지 않고, 이 브릿지에 접속한 휴대폰에만 들립니다. BM 통화 중이면 끝난 뒤에 나갑니다. 휴대폰 화면에는 <b>브릿지 공지</b>로 표시됩니다.</p>
  <div><span class="mut">받을 사용자:</span> <span id="ntWho"></span></div>
  <div id="ntProg" class="hide" style="margin-top:8px;padding:10px 12px;border:1px solid var(--line);border-radius:8px"></div>
  <div class="nsec"><h3>① 문장 읽어 주기</h3>
   <textarea id="ntText" rows="2" maxlength="500" placeholder="예) 잠시 후 10시부터 정기 교신이 있습니다."></textarea>
   <div class="row"><label class="who2"><input type="checkbox" id="ntChime" checked> 딩동 알림음</label>
   <label class="who2">반복 <select id="ntRep"><option value="1">1번</option><option value="2">2번</option><option value="3">3번</option></select></label>
   <button class="pri" onclick="ntSay()">지금 보내기</button><button onclick="ntPreview()" title="휴대폰에는 보내지 않고 이 PC 스피커로만 들어 봅니다">🔈 미리 듣기</button><button class="danger" onclick="ntStop()">공지 모두 멈춤</button>
   <label class="who2"><input type="checkbox" id="ntLocal" onchange="devSave()"> 보낼 때 PC 스피커로도 듣기</label></div>
   <div class="mut" id="ntTts" style="font-size:12px"></div>
   <div class="grid" style="margin-top:8px"><label>호출부호 읽는 방법 (무전 교신 말투)<select id="ntCall"><option value="phonetic">포네틱 — HL0EXM → 호텔 리마 제로 에코 엑스레이 마이크</option><option value="letters">영어 글자 — 에이치 엘 제로 이 엑스 엠</option><option value="korean">한국어 숫자 — 에이치 엘 공 이 엑스 엠</option></select></label>
    <label>발음 사전 (한 줄에 하나: 단어=읽는 법)<textarea id="ntDict" rows="3" placeholder="CQ=씨큐"></textarea></label></div>
   <div class="mut" style="font-size:12px">TG 번호는 한 자리씩 읽습니다(TG 450 → 티지 포 파이브 제로). 바꾼 뒤 [예약·목소리 저장]을 누르고 [미리 듣기]로 들어 보세요.</div></div>
  <div class="nsec"><h3>② 마이크로 바로 말하기</h3>
   <div class="row" style="margin-top:0"><button class="big" id="micBtn" onclick="micToggle()">🎤 방송 시작</button><span class="meter"><i id="micLv"></i></span><span class="mut" id="micTime"></span></div>
   <div class="mut" style="font-size:12px;margin-top:4px">브라우저 보안 규칙 때문에 DVBridge가 실행 중인 PC에서 <b>http://localhost:포트/settings</b> 로 열었을 때만 마이크를 쓸 수 있습니다. 처음에 마이크 허용 창이 뜨면 [허용]을 누르세요.</div></div>
  <div class="nsec"><h3>③ 예약 공지 (정해진 시각에 자동으로 읽기)</h3>
   <div id="schList"></div>
   <div class="row"><button onclick="schAdd()">+ 예약 추가</button></div>
   <div class="grid" style="margin-top:10px"><label>목소리<select id="ntVoice"></select></label>
    <label>말 빠르기 <span id="ntRateV"></span><input type="range" id="ntRate" min="-5" max="5" step="1" style="width:100%" oninput="$('ntRateV').textContent=rateName(this.value)"></label></div>
   <div id="aiBox" class="hide" style="margin-top:10px;padding:10px 12px;border:1px solid var(--line);border-radius:8px">
    <b>자연스러운 목소리(AI 음성 팩) 사용 제한</b>
    <div class="mut" style="font-size:13px;margin:4px 0">음성 팩의 Supertonic 3 모델(Supertone Inc., Open RAIL-M 라이선스)은 다음 용도로 쓸 수 없습니다: 법령 위반, 미성년자에게 해를 끼치는 일, 남을 해치려는 거짓 정보, 남을 해칠 수 있는 개인정보, 명예 훼손·괴롭힘, 다른 사람 흉내(사칭), 차별, 의료 조언, 사법·법 집행·출입국 심사용 정보. 또 만든 소리가 기계로 만든 것임을 알려야 하므로, 휴대폰 화면에 <b>"브릿지 공지 (AI 음성)"</b>으로 표시됩니다. 전문은 프로그램 폴더 voice\supertonic3\LICENSE.</div>
    <label class="who2"><input type="checkbox" id="aiAgree"> 위 사용 제한을 지키겠습니다 (동의해야 자연스러운 목소리를 씀)</label></div>
   <div class="row"><button class="pri" onclick="ntSave()">예약·목소리 저장</button><span class="mut" id="ntRecent" style="font-size:12px"></span></div></div>
  <div class="nsec"><h3>④ 소리 장치 (이 PC)</h3>
   <div class="grid"><label>마이크 (② 마이크 방송에 사용)<select id="devMic" onchange="devSave()"></select></label>
    <label>스피커 (미리 듣기·내 목소리 듣기에 사용)<select id="devSpk" onchange="devSave()"></select></label></div>
   <div class="row"><label class="who2">마이크 크기 <input type="range" id="micGain" min="50" max="300" step="10" value="100" oninput="$('micGainV').textContent=this.value+'%';devSave()"> <span id="micGainV">100%</span></label>
    <button id="micTestBtn" onclick="micTest()">🎙 마이크 시험</button><span class="meter"><i id="testLv"></i></span>
    <label class="who2"><input type="checkbox" id="micMon" onchange="devSave()"> 시험할 때 내 목소리 듣기</label>
    <button class="sm" onclick="devList(true)">장치 목록 새로 고침</button></div>
   <div class="mut" style="font-size:12px;margin-top:4px">장치 이름이 안 보이면 [장치 목록 새로 고침]을 누르고 마이크 허용 창에서 [허용]을 누르세요. 선택은 이 브라우저에 저장됩니다. 스피커 고르기는 Edge·Chrome에서 됩니다.</div></div>
  <div class="msg" id="ntMsg"></div></div>
 <div class="card"><h2>사용자</h2><div class="tbl"><table><thead><tr><th>사용</th><th>콜사인</th><th>핫스팟 ID</th><th>마스터</th><th>USRP 포트</th><th>기본 대상</th><th>비밀번호</th><th></th></tr></thead><tbody id="users"></tbody></table></div>
  <div class="row"><button onclick="edit(-1)">+ 사용자 추가</button></div></div>
 <div class="card hide" id="editor"><h2 id="edTitle">사용자</h2>
  <div class="grid">
   <label>마스터 주소<input type="text" id="f_host" list="masters"></label>
   <label>마스터 포트 (BM은 62031)<input type="number" id="f_port"></label>
   <label>콜사인<input type="text" id="f_callsign"></label>
   <label>DMR ID (7자리)<input type="number" id="f_dmr_id"></label>
   <label>핫스팟 접미사 (0~99, 0이면 ID 그대로)<input type="number" id="f_suffix"></label>
   <label>핫스팟 비밀번호 <span id="pwState" class="tag"></span><input type="password" id="f_password" autocomplete="new-password"></label>
   <label>기본 대상 (TG 또는 개인 ID)<input type="number" id="f_tg"></label>
   <label>슬롯 (1~2)<input type="number" id="f_slot"></label>
   <label>컬러 코드 (0~15)<input type="number" id="f_cc"></label>
   <label>USRP 포트 (휴대폰 앱 TX Port)<input type="number" id="f_usrp_port"></label>
   <label>송신 제한 시간 TOT (초)<input type="number" id="f_tot_seconds"></label>
   <label>마스터 옵션 (비워도 됨)<input type="text" id="f_options"></label>
   <label class="chk"><input type="checkbox" id="f_enabled"> 이 사용자 사용</label>
   <label class="chk"><input type="checkbox" id="f_priv"> 개인호출 (9990·450997 Parrot)</label>
   <label class="chk"><input type="checkbox" id="f_auto_unlink"> TG 바꾸기 전 4000(Unlink) 자동</label>
   <label class="chk" title="BM에 접속하지 않습니다. 말하면 내 목소리가 되돌아오고, 송신·채널 변경은 BM으로 나가지 않습니다 (연습·점검용)"><input type="checkbox" id="f_test_mode"> 시험 모드 — BM에 접속하지 않고, 말하면 에코로 되돌아옴 (연습용)</label>
  </div>
  <datalist id="masters"><option value="4501.master.brandmeister.network"><option value="4502.master.brandmeister.network"></datalist>
  <div class="row"><button class="pri" onclick="saveUser()">저장</button><button onclick="hide('editor')">취소</button></div><div class="msg" id="edMsg"></div></div>
 <div class="card"><h2>네트워크 점검 (내부망·외부망)</h2>
  <p class="mut" style="margin:0 0 6px">이 PC의 주소, 공유기 뒤인지, 포트·방화벽 상태, 인터넷 연결을 확인하고 해야 할 일을 알려 줍니다. 설정은 바꾸지 않습니다.</p>
  <div class="row" style="margin-top:0"><button class="pri" id="ncBtn" onclick="netcheck(false)">점검하기</button>
  <button id="ncBtn2" onclick="netcheck(true)" title="외부 서비스(api.ipify.org)에 이 PC의 공인 IP를 물어봅니다">공인 IP도 확인</button><span class="mut" id="ncAt"></span></div>
  <div id="nc"></div></div>
 <div class="card"><h2>보코더 (음성)</h2><div id="voc" class="mut"></div></div>
 <div class="card"><h2>ID 명단 (콜사인·이름 표시)</h2><div id="ids" class="mut"></div>
  <div class="row"><button id="idBtn" onclick="getIds()">RadioID 명단 받기</button></div></div>
 <div class="card"><h2>설정 백업·복원</h2>
  <p class="mut" style="margin:0 0 6px">설정을 바꿀 때마다 자동 백업(최근 20개)도 남습니다. 핫스팟 비밀번호는 이 PC의 같은 Windows 계정에서만 그대로 복원됩니다.</p>
  <div class="row"><button onclick="location.href='/api/admin/backup'">백업 파일 내려받기</button>
  <label style="display:inline-block"><input type="file" id="rf" accept=".ini" class="hide" onchange="restore(this)"><span class="mut"></span></label>
  <button onclick="$('rf').click()">백업 파일로 복원…</button></div><div class="msg" id="bMsg"></div></div>
 <div class="card"><h2>관리자 비밀번호 바꾸기</h2><div class="grid">
   <label>현재 비밀번호<input type="password" id="p0" autocomplete="current-password"></label>
   <label>새 비밀번호 (6자 이상)<input type="password" id="p1" autocomplete="new-password"></label>
   <label>한 번 더<input type="password" id="p2" autocomplete="new-password"></label></div>
  <div class="row"><button onclick="changePw()">바꾸기</button></div><div class="msg" id="pMsg"></div></div>
 <p class="mut" style="font-size:12px">※ 핫스팟 비밀번호는 암호화해 저장하며 화면에 다시 표시하지 않습니다. HTTPS가 아니므로 비밀번호 입력은 이 PC나 믿을 수 있는 네트워크에서 하세요.</p>
</div>
</main>
<script>
const $=id=>document.getElementById(id);
const THEMES=['auto','light','dark'],THEME_NAME={auto:'자동',light:'밝게',dark:'어둡게'};
function applyTheme(t){if(t==='auto')document.documentElement.removeAttribute('data-theme');else document.documentElement.setAttribute('data-theme',t);$('theme').textContent='테마: '+THEME_NAME[t];}
function loadTheme(){try{const t=localStorage.getItem('dvbridge-theme');return THEMES.includes(t)?t:'auto'}catch(e){return 'auto'}}
function cycleTheme(){const t=THEMES[(THEMES.indexOf(loadTheme())+1)%3];try{localStorage.setItem('dvbridge-theme',t)}catch(e){}applyTheme(t);}
applyTheme(loadTheme());
function esc(s){return String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function show(id){$(id).classList.remove('hide')}function hide(id){$(id).classList.add('hide')}
function msg(id,t,ok){const e=$(id);e.textContent=t||'';e.className='msg '+(ok?'ok':'err');}
async function api(path,body){
 const r=await fetch(path,body===undefined?{cache:'no-store',credentials:'same-origin'}:{method:'POST',credentials:'same-origin',headers:{'Content-Type':'application/json','X-DVBridge':'1'},body:JSON.stringify(body)});
 let j={};try{j=await r.json()}catch(e){}
 if(r.status===401&&path!=='/api/admin/login'){await init();}
 return {ok:r.ok&&j.ok!==false,status:r.status,data:j};
}
let cfg=null;
async function init(){
 const st=(await api('/api/admin/state')).data;
 ['setup','login','app'].forEach(hide);hide('logout');
 if(!st.has_password){show('setup');if(!st.local){$('setupText').textContent='아직 관리자 비밀번호가 없습니다. DVBridge가 실행 중인 PC의 브라우저에서 http://localhost:포트/settings 로 먼저 정해 주세요.';hide('setupForm');}return;}
 if(!st.logged_in){show('login');$('lp').focus();return;}
 show('app');show('logout');await load();
}
async function setup(){if($('s1').value!==$('s2').value)return msg('setupMsg','두 비밀번호가 다릅니다');const r=await api('/api/admin/setup',{password:$('s1').value});if(!r.ok)return msg('setupMsg',r.data.message||'실패');$('s1').value=$('s2').value='';init();}
async function login(){const r=await api('/api/admin/login',{password:$('lp').value});$('lp').value='';if(!r.ok)return msg('loginMsg',r.data.message||'실패');msg('loginMsg','');init();}
async function logout(){await api('/api/admin/logout',{});init();}
async function load(){
 const r=await api('/api/admin/config');if(!r.ok)return;cfg=r.data;
 if(document.activeElement!==$('rxOnly'))$('rxOnly').checked=cfg.rx_target_only!==false;
 if(cfg.intercom_tg!==icTg||cfg.echo_tg!==ecTg){icTg=cfg.intercom_tg;ecTg=cfg.echo_tg;QUICK=QUICK0.concat(icTg?[{tg:icTg,l:icTg+' 내부 채널'}]:[],ecTg?[{tg:ecTg,l:ecTg+' 내부 에코'}]:[]);tgCount=-1;if(document.activeElement!==$('icTg'))$('icTg').value=icTg;if(document.activeElement!==$('ecTg'))$('ecTg').value=ecTg;}
 renderTg();renderNotice();
 if(cfg.netcheck&&!cfg.netcheck.running&&!$('nc').innerHTML)showNet();
 $('runState').innerHTML=cfg.service_on?'<b style="color:var(--ok)">● 서비스 켜짐</b>':'<b class="mut">○ 서비스 꺼짐</b>';
 $('voc').innerHTML=(cfg.vocoders||[]).length?'사용 중: '+cfg.vocoders.map(esc).join(', ')+'<br><span class="mut">보코더 폴더: <code>'+esc(cfg.plugin_dir)+'</code></span>':'<b style="color:var(--tx)">보코더 플러그인이 없습니다</b> — 음성이 나오지 않습니다. DVBridge에는 보코더가 들어 있지 않으니, AMBE+2 보코더 플러그인(vocoder_*.dll)을 직접 구해(라이선스·특허 조건은 각자 확인) 아래 폴더에 넣고 DVBridge를 다시 시작하세요.<br><code>'+esc(cfg.plugin_dir)+'</code>';
 $('ids').textContent=(cfg.iddb_count?cfg.iddb_count.toLocaleString()+'명 ('+cfg.iddb_date+' 받음)':'명단 없음')+(cfg.iddb_status?' — '+cfg.iddb_status:'');
 $('idBtn').disabled=!!cfg.iddb_busy;if(cfg.iddb_busy)setTimeout(load,1500);
 $('users').innerHTML=(cfg.users||[]).length?cfg.users.map((u,i)=>'<tr><td>'+(u.enabled?'✔':'')+'</td><td><b>'+esc(u.callsign)+'</b>'+(u.test_mode?' <span class="tag" style="color:var(--acc)">시험 모드</span>':'')+'</td><td>'+esc(u.hotspot_id)+'</td><td>'+esc(u.host)+':'+esc(u.port)+'</td><td>'+esc(u.usrp_port)+'</td><td>'+(u.priv?esc(u.tg)+' <span class="tag">개인</span>':'TG '+esc(u.tg))+'</td><td>'+(u.password_set?'<span class="tag">저장됨</span>':'<span class="tag" style="color:var(--tx)">없음</span>')+'</td><td><button class="sm" onclick="testSound('+i+')" title="이 사용자의 휴대폰에만 시험 소리를 보냅니다 (BM과 무관)">🔊 시험</button> <button class="sm" onclick="edit('+i+')">편집</button> <button class="sm danger" onclick="del('+i+')">삭제</button></td></tr>').join(''):'<tr><td colspan="8" class="mut">사용자가 없습니다 — [+ 사용자 추가]</td></tr>';
}
// ---- 사용자별 TG ----
const QUICK0=[{tg:450,l:'450 한국'},{tg:91,l:'91 전세계'},{tg:9990,l:'9990 에코',p:true},{tg:450997,l:'450997 에코',p:true},{tg:4000,l:'4000 끊기'}];
let QUICK=QUICK0;
function who(u){return esc(u.callsign)+(u.suffix?'·'+String(u.suffix).padStart(2,'0'):'')}
let tgCount=-1,icTg=null,ecTg=null;
async function icSave(){const r=await api('/api/admin/action',{action:'intercom_save',tg:Number($('icTg').value||0),echo_tg:Number($('ecTg').value||0)});msg('icMsg',r.data.message||(r.ok?'저장했습니다':'실패'),r.ok);load();}
function renderTg(){
 const us=cfg.users||[];
 if(us.length!==tgCount){
  tgCount=us.length;
  $('tgs').innerHTML=us.length?us.map((u,i)=>'<div class="tgrow"><span class="who">'+who(u)+'</span><span class="now" id="tgNow'+i+'"></span>'+
   '<input type="number" id="tgIn'+i+'" placeholder="TG 번호" onkeydown="if(event.key===\'Enter\')setTg('+i+')"><label style="display:inline;color:var(--fg)"><input type="checkbox" id="tgP'+i+'"> 개인</label>'+
   '<button class="pri sm" onclick="setTg('+i+')">바꾸기</button>'+QUICK.map(q=>'<button class="qb" onclick="quickTg('+i+','+q.tg+','+(q.p?1:0)+')">'+q.l+'</button>').join('')+'</div>').join(''):'<span class="mut">사용자가 없습니다</span>';
 }
 us.forEach((u,i)=>{const e=$('tgNow'+i);if(!e)return;
  e.innerHTML='지금: <b>'+(u.live_tg===cfg.intercom_tg&&cfg.intercom_tg?'내부 채널 '+esc(u.live_tg):u.live_tg===cfg.echo_tg&&cfg.echo_tg?'내부 에코 '+esc(u.live_tg):u.live_priv?esc(u.live_tg)+' (개인)':'TG '+esc(u.live_tg))+'</b> · '+(u.running?(u.test_mode?'<span style="color:var(--acc)">시험 모드 (BM 안 씀)</span>':u.connected?'<span style="color:var(--ok)">접속됨</span>':'<span style="color:var(--acc)">접속 중…</span>'):'<span class="mut">꺼짐</span>')+(u.client?' · 휴대폰'+netTag(u.client_net):'');});
}
// ---- 브릿지 공지 ----
const DAYS=['월','화','수','목','금','토','일'];
let ntInit=false,sch=[],ntWhoCount=-1;
function rateName(v){v=Number(v);return v===0?'(보통)':v<0?'(느리게 '+(-v)+')':'(빠르게 '+v+')'}
function ntTargets(){const all=$('ntAll');if(!all||all.checked)return [];return [...document.querySelectorAll('.ntU:checked')].map(e=>e.value);}
function ntAllChanged(c){document.querySelectorAll('.ntU').forEach(e=>e.disabled=c);}
function renderNotice(){
 const n=cfg.notice||{},us=cfg.users||[];
 if(us.length!==ntWhoCount){ntWhoCount=us.length;
  $('ntWho').innerHTML='<label class="who2"><input type="checkbox" id="ntAll" checked onchange="ntAllChanged(this.checked)"> 모두</label>'+
   us.map(u=>'<label class="who2"><input type="checkbox" class="ntU" value="'+esc(u.key)+'" checked disabled> '+who(u)+'</label>').join('');}
 renderProg(n,us);
 $('ntTts').textContent=n.tts_ok?'':(n.voices&&!n.voices.length?'이 PC에 음성 합성 목소리가 없어 문장 읽기는 쓸 수 없습니다 (마이크는 됩니다)':'');
 $('ntRecent').textContent=(n.recent||[]).length?'최근 예약 실행: '+n.recent.slice(0,3).map(r=>r.at+' '+r.text).join(' / '):'';
 if(!ntInit&&n.config){ntInit=true;const c=n.config;
  sch=(c.schedule||[]).map(x=>Object.assign({},x));renderSch();
  $('ntChime').checked=c.chime!==false;$('ntRate').value=c.rate||0;$('ntRateV').textContent=rateName(c.rate||0);
  $('ntVoice').dataset.want=c.voice||'';$('aiAgree').checked=!!c.ai_agree;$('ntCall').value=c.callstyle||'phonetic';$('ntDict').value=c.dict??'';
 }
 const vs=n.voices||[];
 $('aiBox').classList.toggle('hide',!n.voicepack);
 if($('ntVoice').options.length!==vs.length+1){const want=$('ntVoice').value||$('ntVoice').dataset.want||'';
  $('ntVoice').innerHTML='<option value="">자동 ('+(vs.some(v=>v.engine==='supertonic')?'자연스러운 목소리 · 여성 1':'Windows 한국어 목소리')+')</option>'+vs.map(v=>'<option value="'+esc(v.name)+'">'+esc(v.label||(v.name+' ('+v.culture+')'))+'</option>').join('');$('ntVoice').value=want;}
}
// 보내기 진행 표시: ① 음성 만들기(예상 시간 대비) → ② 기다림(이유) → ③ 보내는 중(초) → 끝
let progFast=null,progSeen=false;
function bar(p,cls){return '<span class="meter" style="width:220px"><i style="width:'+Math.round(Math.max(0,Math.min(1,p))*100)+'%'+(cls?';background:'+cls:'')+'"></i></span>'}
function renderProg(n,us){
 const j=n.job||{},rows=[];
 if(j.making){const e=j.elapsed_ms/1000,t=Math.max(j.estimate_ms/1000,e+0.5);
  rows.push('<div>① '+(j.preview?'미리 듣기 ':'')+'음성 만드는 중'+(j.ai?' (자연스러운 목소리)':'')+' '+bar(e/t)+' <b>'+e.toFixed(1)+'초</b> / 약 '+t.toFixed(0)+'초</div>');}
 if(j.queue)rows.push('<div class="mut">만들 차례를 기다리는 공지 '+j.queue+'건</div>');
 us.forEach(u=>{const s=u.notice_status;if(!s||s.state==='idle')return;
  if(s.state==='sending')rows.push('<div>③ <b>'+who(u)+'</b> 휴대폰으로 보내는 중 '+bar(s.progress,'var(--tx)')+' '+(s.live?'실시간':s.pos_s.toFixed(1)+' / '+s.total_s.toFixed(1)+'초')+(s.waiting>1?' <span class="mut">(뒤에 '+(s.waiting-1)+'건)</span>':'')+'</div>');
  else rows.push('<div>② <b>'+who(u)+'</b> <span style="color:var(--acc)">'+esc(s.reason||'기다리는 중')+'</span> <span class="mut">('+(s.live?'실시간':s.total_s.toFixed(1)+'초 분량')+', 끝나면 바로 나감)</span></div>');});
 const busy=rows.length>0;
 if(busy){progSeen=true;$('ntProg').innerHTML=rows.join('');show('ntProg');}
 else if(progSeen){progSeen=false;$('ntProg').innerHTML='<div style="color:var(--ok)">✔ 공지를 다 보냈습니다</div>';show('ntProg');setTimeout(()=>{if(!progSeen)hide('ntProg')},4000);}
 // 진행 중에는 0.5초마다 새로 고침 (평소 3초)
 if(busy&&!progFast)progFast=setInterval(()=>{if(document.visibilityState==='visible')load()},500);
 if(!busy&&progFast){clearInterval(progFast);progFast=null;}
}
function schRow(x,i){
 const us=cfg.users||[],tg=(x.targets||[])[0]||'';
 return '<div class="sch"><label class="who2"><input type="checkbox" '+(x.on!==false?'checked':'')+' onchange="sch['+i+'].on=this.checked"> 사용</label>'+
  '<input type="time" value="'+esc(x.time||'09:00')+'" onchange="sch['+i+'].time=this.value">'+
  '<span>'+DAYS.map((d,k)=>'<span class="day'+((x.days||[]).includes(k+1)?' on':'')+'" onclick="schDay('+i+','+(k+1)+',this)">'+d+'</span>').join(' ')+'</span>'+
  '<input type="text" class="txt" maxlength="500" placeholder="읽어 줄 문장" value="'+esc(x.text||'')+'" oninput="sch['+i+'].text=this.value">'+
  '<select onchange="sch['+i+'].targets=this.value?[this.value]:[]"><option value="">모든 사용자</option>'+us.map(u=>'<option value="'+esc(u.key)+'"'+(u.key===tg?' selected':'')+'>'+who(u)+'</option>').join('')+'</select>'+
  '<label class="who2"><input type="checkbox" '+(x.chime!==false?'checked':'')+' onchange="sch['+i+'].chime=this.checked"> 딩동</label>'+
  '<select onchange="sch['+i+'].repeat=Number(this.value)">'+[1,2,3].map(r=>'<option value="'+r+'"'+((x.repeat||1)===r?' selected':'')+'>'+r+'번</option>').join('')+'</select>'+
  '<button class="sm" onclick="schTest('+i+')" title="이 문장을 지금 한 번 보내 봅니다">지금 시험</button><button class="sm danger" onclick="schDel('+i+')">삭제</button></div>';
}
function renderSch(){$('schList').innerHTML=sch.length?sch.map(schRow).join(''):'<p class="mut" style="margin:0">예약이 없습니다. [+ 예약 추가]를 누르세요.</p>';}
function schDay(i,d,el){const a=sch[i].days||(sch[i].days=[]);const k=a.indexOf(d);if(k<0)a.push(d);else a.splice(k,1);el.classList.toggle('on');}
function schAdd(){sch.push({on:true,time:'09:00',days:[1,2,3,4,5,6,7],text:'',targets:[],chime:true,repeat:1});renderSch();}
function schDel(i){sch.splice(i,1);renderSch();}
async function schTest(i){const x=sch[i];if(!(x.text||'').trim())return msg('ntMsg','문장을 넣어 주세요');
 const r=await api('/api/admin/action',{action:'notice_say',text:x.text,targets:x.targets||[],chime:x.chime!==false,repeat:x.repeat||1});msg('ntMsg',r.data.message||(r.ok?'보냈습니다':'실패'),r.ok);}
async function ntSave(){
 const r=await api('/api/admin/action',{action:'notice_save',notice:{voice:$('ntVoice').value,rate:Number($('ntRate').value),chime:$('ntChime').checked,ai_agree:$('aiAgree').checked,callstyle:$('ntCall').value,dict:$('ntDict').value,schedule:sch}});
 msg('ntMsg',r.data.message||(r.ok?'저장했습니다':'실패'),r.ok);if(r.ok){ntInit=false;load();}
}
async function ntSay(){
 const t=$('ntText').value.trim();if(!t)return msg('ntMsg','안내할 문장을 넣어 주세요');
 const w0=(cfg.notice||{}).wav_id||0;
 const r=await api('/api/admin/action',{action:'notice_say',text:t,targets:ntTargets(),chime:$('ntChime').checked,repeat:Number($('ntRep').value)});
 msg('ntMsg',r.data.message||(r.ok?'보냈습니다':'실패'),r.ok);setTimeout(load,200);
 if(r.ok&&$('ntLocal').checked)playNotice(w0);
}
async function ntStop(){if(mic)await micStop();const r=await api('/api/admin/action',{action:'notice_stop'});msg('ntMsg',r.data.message||'',r.ok);setTimeout(load,500);}
// ---- 소리 장치 (이 브라우저에 저장) ----
const DEVKEY='dvbridge-notice-dev';
function devLoad(){try{return JSON.parse(localStorage.getItem(DEVKEY)||'{}')}catch(e){return {}}}
function devSave(){const d={mic:$('devMic').value,spk:$('devSpk').value,gain:Number($('micGain').value),mon:$('micMon').checked,local:$('ntLocal').checked};try{localStorage.setItem(DEVKEY,JSON.stringify(d))}catch(e){}}
function micGainVal(){return Number($('micGain').value||100)/100}
function micConstraints(){const c={echoCancellation:true,noiseSuppression:true,autoGainControl:true};const id=$('devMic').value;if(id)c.deviceId={exact:id};return c;}
async function devList(ask){
 if(!navigator.mediaDevices||!navigator.mediaDevices.enumerateDevices){$('devMic').innerHTML='<option value="">이 주소에서는 장치를 고를 수 없습니다 (localhost로 여세요)</option>';return;}
 if(ask){try{const st=await navigator.mediaDevices.getUserMedia({audio:true});st.getTracks().forEach(t=>t.stop());}catch(e){msg('ntMsg','마이크 허용이 필요합니다: '+e.message);}}
 const ds=await navigator.mediaDevices.enumerateDevices(),d=devLoad();
 const opt=(k,def)=>'<option value="">'+def+'</option>'+ds.filter(x=>x.kind===k&&x.deviceId&&x.deviceId!=='default'&&x.deviceId!=='communications').map((x,i)=>'<option value="'+esc(x.deviceId)+'">'+esc(x.label||((k==='audioinput'?'마이크 ':'스피커 ')+(i+1)))+'</option>').join('');
 $('devMic').innerHTML=opt('audioinput','Windows 기본 마이크');$('devSpk').innerHTML=opt('audiooutput','Windows 기본 스피커');
 $('devMic').value=[...$('devMic').options].some(o=>o.value===d.mic)?d.mic:'';$('devSpk').value=[...$('devSpk').options].some(o=>o.value===d.spk)?d.spk:'';
}
function devInit(){const d=devLoad();if(d.gain){$('micGain').value=d.gain;$('micGainV').textContent=d.gain+'%';}$('micMon').checked=!!d.mon;$('ntLocal').checked=!!d.local;devList(false);
 if(navigator.mediaDevices&&navigator.mediaDevices.addEventListener)navigator.mediaDevices.addEventListener('devicechange',()=>devList(false));}
async function toSpeaker(el){const id=$('devSpk').value;if(id&&el.setSinkId){try{await el.setSinkId(id)}catch(e){msg('ntMsg','고른 스피커를 쓸 수 없어 기본 스피커로 냅니다')}}}
// 서버가 새로 만든 공지 음성(wav_id가 바뀜)을 받아 PC 스피커로 재생
async function playNotice(w0){
 for(let k=0;k<60;k++){await new Promise(r=>setTimeout(r,700));const c=(await api('/api/admin/config')).data;if(c&&c.notice&&c.notice.wav_id>w0){cfg=c;break;}if(k===59)return msg('ntMsg','미리 듣기 음성을 받지 못했습니다');}
 const resp=await fetch('/api/admin/notice.wav',{cache:'no-store',credentials:'same-origin'});if(!resp.ok)return msg('ntMsg','미리 듣기 음성을 받지 못했습니다');
 const a=new Audio(URL.createObjectURL(await resp.blob()));await toSpeaker(a);try{await a.play()}catch(e){msg('ntMsg','소리를 낼 수 없습니다: '+e.message)}
}
async function ntPreview(){
 const t=$('ntText').value.trim();if(!t)return msg('ntMsg','미리 들을 문장을 넣어 주세요');
 const w0=(cfg.notice||{}).wav_id||0;
 const r=await api('/api/admin/action',{action:'notice_preview',text:t,chime:$('ntChime').checked});
 msg('ntMsg',r.ok?'미리 듣기 음성을 만드는 중… (휴대폰에는 보내지 않음)':(r.data.message||'실패'),r.ok);if(r.ok)playNotice(w0);
}
// 마이크 시험: 보내지 않고 크기만 보기 (원하면 스피커로 내 목소리 듣기)
let mtest=null;
async function micTest(){
 if(mtest){try{clearInterval(mtest.timer);mtest.stream.getTracks().forEach(t=>t.stop());mtest.ac.close();}catch(e){}mtest=null;$('micTestBtn').textContent='🎙 마이크 시험';$('testLv').style.width='0';return;}
 if(!window.isSecureContext||!navigator.mediaDevices)return msg('ntMsg','이 주소에서는 마이크를 쓸 수 없습니다 (localhost로 여세요)');
 let stream;try{stream=await navigator.mediaDevices.getUserMedia({audio:micConstraints()});}catch(e){return msg('ntMsg','마이크를 열 수 없습니다: '+e.message);}
 const ac=new (window.AudioContext||window.webkitAudioContext)(),src=ac.createMediaStreamSource(stream),g=ac.createGain(),an=ac.createAnalyser();
 g.gain.value=micGainVal();src.connect(g);g.connect(an);
 if($('micMon').checked){const id=$('devSpk').value;if(id&&ac.setSinkId){try{await ac.setSinkId(id)}catch(e){}}g.connect(ac.destination);}
 const buf=new Float32Array(an.fftSize);
 mtest={stream,ac,timer:setInterval(()=>{g.gain.value=micGainVal();an.getFloatTimeDomainData(buf);let e=0;for(const v of buf)e+=v*v;$('testLv').style.width=Math.min(100,Math.sqrt(e/buf.length)*400)+'%';},100)};
 $('micTestBtn').textContent='■ 시험 끝';msg('ntMsg','말해 보세요. 막대가 절반쯤 움직이면 알맞습니다'+($('micMon').checked?' (헤드폰을 쓰면 울림이 없습니다)':''),true);
}
// 마이크: 브라우저에서 8kHz 16bit로 줄여 0.2초마다 보냄 (순서대로)
let mic=null;
function micToggle(){mic?micStop():micStart();}
async function micStart(){
 if(!window.isSecureContext||!navigator.mediaDevices||!navigator.mediaDevices.getUserMedia)return msg('ntMsg','이 주소에서는 마이크를 쓸 수 없습니다. DVBridge가 실행 중인 PC에서 http://localhost:'+(location.port||80)+'/settings 로 열어 주세요');
 let stream;
 try{stream=await navigator.mediaDevices.getUserMedia({audio:micConstraints()});}
 catch(e){return msg('ntMsg','마이크를 열 수 없습니다 (허용 안 함 또는 마이크 없음): '+e.message);}
 const ac=new (window.AudioContext||window.webkitAudioContext)();
 const src=ac.createMediaStreamSource(stream),sp=ac.createScriptProcessor(4096,1,1);
 const m={stream,ac,src,sp,ratio:ac.sampleRate/8000,acc:0,sum:0,cnt:0,out:[],q:Promise.resolve(),t0:Date.now(),targets:ntTargets(),fail:false};
 sp.onaudioprocess=e=>{
  const x=e.inputBuffer.getChannelData(0);
  for(let i=0;i<x.length;i++){m.sum+=x[i];m.cnt++;m.acc+=1;if(m.acc>=m.ratio){m.acc-=m.ratio;m.out.push(m.sum/m.cnt);m.sum=0;m.cnt=0;}}
  while(m.out.length>=1600)micSend(m,m.out.splice(0,1600),false);
 };
 src.connect(sp);sp.connect(ac.destination);
 mic=m;$('micBtn').textContent='■ 방송 끝';$('micBtn').classList.add('onair');msg('ntMsg','방송 중입니다. 말씀하세요.',true);
 m.timer=setInterval(()=>{const s=Math.round((Date.now()-m.t0)/1000);$('micTime').textContent=Math.floor(s/60)+':'+String(s%60).padStart(2,'0');},500);
}
function micSend(m,f,end){
 const b=new Uint8Array(f.length*2);let e=0;
 const g=micGainVal();for(let i=0;i<f.length;i++){const v=Math.max(-1,Math.min(1,f[i]*g))*32767|0;b[i*2]=v&255;b[i*2+1]=(v>>8)&255;e+=f[i]*f[i];}
 if(f.length)$('micLv').style.width=Math.min(100,Math.sqrt(e/f.length)*400)+'%';
 let s='';for(let i=0;i<b.length;i+=8192)s+=String.fromCharCode.apply(null,b.subarray(i,i+8192));
 const body={action:'notice_live',pcm:btoa(s),end:end,targets:m.targets};
 m.q=m.q.then(async()=>{const r=await api('/api/admin/action',body);if(!r.ok&&!m.fail){m.fail=true;msg('ntMsg',r.data.message||'보내지 못했습니다');}});
 return m.q;
}
async function micStop(){
 const m=mic;if(!m)return;mic=null;
 clearInterval(m.timer);try{m.sp.disconnect();m.src.disconnect();m.stream.getTracks().forEach(t=>t.stop());m.ac.close();}catch(e){}
 $('micBtn').textContent='🎤 방송 시작';$('micBtn').classList.remove('onair');$('micLv').style.width='0';$('micTime').textContent='';
 await micSend(m,m.out.splice(0),true);if(!m.fail)msg('ntMsg','방송을 마쳤습니다',true);
}
function netTag(k){return k?'<span class="net '+(k==='외부망'?'ext':k==='내부망'?'int':'')+'">'+esc(k)+'</span>':''}
// ---- 네트워크 점검 ----
let ncPoll=null;
async function netcheck(pub){
 const r=await api('/api/admin/action',{action:'netcheck',public_ip:pub});
 if(!r.ok)return msg('topMsg',r.data.message||'실패');
 $('nc').innerHTML='<p class="mut">점검 중… (최대 20초)</p>';$('ncBtn').disabled=$('ncBtn2').disabled=true;
 clearInterval(ncPoll);ncPoll=setInterval(async()=>{const c=(await api('/api/admin/config')).data;if(c.netcheck&&!c.netcheck.running){clearInterval(ncPoll);cfg=c;showNet();}},1000);
}
function showNet(){
 const n=cfg&&cfg.netcheck;if(!n||n.running===undefined)return;
 $('ncBtn').disabled=$('ncBtn2').disabled=!!n.running;
 $('ncAt').textContent=n.checked_at?' 마지막 점검: '+n.checked_at:'';
 if(n.running)return;
 const ifs=(n.interfaces||[]).map(i=>'<tr><td>'+esc(i.name)+'</td><td>'+esc(i.ip)+(i.ip===n.outbound_ip?' <span class="tag">인터넷으로 나가는 쪽</span>':'')+'</td><td>'+netTag(i.kind)+'</td></tr>').join('');
 $('nc').innerHTML=(ifs?'<div class="tbl"><table><thead><tr><th>연결</th><th>이 PC 주소</th><th>종류</th></tr></thead><tbody>'+ifs+'</tbody></table></div>':'')+
  (n.public_ip?'<p style="margin:8px 0 0">공인 IP: <b>'+esc(n.public_ip)+'</b></p>':'')+
  '<ul class="adv">'+(n.advice||[]).map(a=>'<li class="'+esc(a.level)+'">'+esc(a.text)+'</li>').join('')+'</ul>';
}
async function sendTg(i,tg,priv){
 if(!tg)return msg('tgMsg','TG 번호를 넣으세요');
 const r=await api('/api/admin/action',{action:'set_tg',index:i,tg:tg,priv:!!priv,save_default:$('tgSave').checked});
 msg('tgMsg',r.data.message||(r.ok?'바꿨습니다':'실패'),r.ok);setTimeout(load,600);
}
function setTg(i){sendTg(i,Number($('tgIn'+i).value),$('tgP'+i).checked);}
function quickTg(i,tg,p){$('tgIn'+i).value=tg;$('tgP'+i).checked=!!p;sendTg(i,tg,p);}
async function getIds(){const r=await api('/api/admin/action',{action:'download_ids'});msg('topMsg',r.data.message||'',r.ok);setTimeout(load,800);}
function restore(inp){const f=inp.files[0];inp.value='';if(!f)return;if(f.size>60000)return msg('bMsg','설정 백업 파일이 아닙니다 (너무 큼)');
 if(!confirm(f.name+' 으로 설정을 되돌릴까요?\n서비스는 꺼지고, 지금 설정은 자동으로 보관됩니다.'))return;
 const rd=new FileReader();rd.onload=async()=>{const r=await api('/api/admin/action',{action:'restore',ini:rd.result});msg('bMsg',r.data.message||(r.ok?'복원했습니다':'실패'),r.ok);load();};rd.readAsText(f);}
async function rxSave(){const r=await api('/api/admin/action',{action:'rx_filter_save',on:$('rxOnly').checked});msg('rxMsg',r.data.message||(r.ok?'저장했습니다':'실패'),r.ok);}
async function testSound(i){const r=await api('/api/admin/action',{action:'test_sound',index:i});msg('topMsg',r.data.message||(r.ok?'보냈습니다':'실패'),r.ok);}
let editing=-1;
const F=['host','port','callsign','dmr_id','suffix','tg','slot','cc','usrp_port','tot_seconds','options'],B=['enabled','priv','auto_unlink','test_mode'];
function edit(i){
 editing=i;const d=cfg.defaults||{};const u=i>=0?cfg.users[i]:Object.assign({},d,{usrp_port:cfg.next_usrp_port||d.usrp_port});
 F.forEach(k=>$('f_'+k).value=(k==='dmr_id'&&!u[k])?'':(u[k]??''));B.forEach(k=>$('f_'+k).checked=!!u[k]);	// 새 사용자의 DMR ID는 빈 칸
 $('f_password').value='';$('f_password').placeholder=u.password_set?'바꿀 때만 입력':'핫스팟 비밀번호 입력';
 $('pwState').textContent=u.password_set?'저장됨':'필수';
 $('edTitle').textContent=i>=0?'사용자 편집 — '+u.callsign:'사용자 추가';msg('edMsg','');show('editor');$('editor').scrollIntoView({behavior:'smooth'});
}
async function saveUser(){
 const u={};F.forEach(k=>{const v=$('f_'+k).value;u[k]=$('f_'+k).type==='number'?Number(v):v});B.forEach(k=>u[k]=$('f_'+k).checked);
 if($('f_password').value)u.password=$('f_password').value;
 const r=await api('/api/admin/action',{action:'save_user',index:editing,user:u});$('f_password').value='';
 if(!r.ok)return msg('edMsg',r.data.message||'실패');hide('editor');msg('topMsg',r.data.message||'저장했습니다',true);load();
}
async function del(i){if(!confirm(cfg.users[i].callsign+' 사용자를 삭제할까요?'))return;const r=await api('/api/admin/action',{action:'delete_user',index:i});msg('topMsg',r.data.message||(r.ok?'삭제했습니다':'실패'),r.ok);load();}
async function act(a){const r=await api('/api/admin/action',{action:a});msg('topMsg',r.data.message||(r.ok?'완료':'실패'),r.ok);setTimeout(load,800);}
async function changePw(){if($('p1').value!==$('p2').value)return msg('pMsg','새 비밀번호 두 개가 다릅니다');const r=await api('/api/admin/action',{action:'change_password',old:$('p0').value,new:$('p1').value});['p0','p1','p2'].forEach(k=>$(k).value='');msg('pMsg',r.data.message||(r.ok?'바꿨습니다':'실패'),r.ok);}
init();devInit();
// 접속 상태·지금 TG를 3초마다 갱신 (입력 칸은 건드리지 않음)
setInterval(()=>{if(!$('app').classList.contains('hide')&&document.visibilityState==='visible')load();},3000);
</script></body></html>)HTML");
}
