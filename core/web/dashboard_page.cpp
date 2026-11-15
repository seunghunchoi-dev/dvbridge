// SPDX-License-Identifier: MIT
// Copyright (c) 2026 최승훈 (Choi Seunghun, 6K2KZR)
// 대시보드 페이지 (외부 라이브러리·인터넷 없이 동작, 밝은/어두운 테마)
//   사용자 한 줄 표 + 실시간 듣기(Web Audio, 8kHz PCM 스트림) + 서버 부하
#include "dashboard_server.h"

QByteArray DashboardServer::page_html()
{
	return QByteArray(R"HTML(<!doctype html>
<html lang="ko"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DVBridge 대시보드</title>
<link rel="icon" type="image/png" href="data:image/png;base64,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">
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#6b7280;--line:#e5e7eb;--tx:#c0392b;--rx:#1e8449;--acc:#d35400;--off:#9ca3af;color-scheme:light}
:root[data-theme=dark]{--bg:#121417;--card:#1c1f24;--fg:#e8eaed;--mut:#9aa0a6;--line:#2c3037;--tx:#e74c3c;--rx:#27ae60;--acc:#e67e22;--off:#5f6368;color-scheme:dark}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#121417;--card:#1c1f24;--fg:#e8eaed;--mut:#9aa0a6;--line:#2c3037;--tx:#e74c3c;--rx:#27ae60;--acc:#e67e22;--off:#5f6368;color-scheme:dark}}
button.th{border:1px solid rgba(255,255,255,.5);background:transparent;color:#fff;border-radius:6px;padding:2px 9px;cursor:pointer;font:inherit;font-size:12px;margin-left:10px}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,"Malgun Gothic",sans-serif}
header{background:#c0392b;color:#fff;padding:9px 16px;display:flex;justify-content:space-between;align-items:center}
header b{font-size:18px}header small{opacity:.85}
main{max-width:1200px;margin:0 auto;padding:12px 16px}
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px;margin-bottom:12px}
.st{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:6px 10px}
.st .k{color:var(--mut);font-size:11px}.st .v{font-size:16px;font-weight:600;font-variant-numeric:tabular-nums}
.bar{height:4px;background:var(--line);border-radius:2px;margin-top:3px;overflow:hidden}.bar i{display:block;height:100%;background:var(--rx)}
.bar i.hi{background:var(--acc)}.bar i.crit{background:var(--tx)}
h2{font-size:15px;margin:14px 0 6px}
.tbl{overflow-x:auto;background:var(--card);border:1px solid var(--line);border-radius:8px}
table{width:100%;border-collapse:collapse}th,td{padding:7px 10px;border-bottom:1px solid var(--line);text-align:left;white-space:nowrap}
th{font-size:12px;color:var(--mut);font-weight:600}tr:last-child td{border-bottom:0}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--off);margin-right:6px;vertical-align:middle}
.dot.on{background:var(--rx)}.dot.wait{background:var(--acc)}
.now{font-weight:600}.now.tx{color:var(--tx)}.now.rx{color:var(--rx)}
tr.tx td{background:color-mix(in srgb,var(--tx) 10%,transparent)}tr.rx td{background:color-mix(in srgb,var(--rx) 10%,transparent)}
button.ls{border:1px solid var(--line);background:var(--bg);color:var(--fg);border-radius:6px;padding:3px 9px;cursor:pointer;font:inherit}
button.ls.on{background:var(--rx);color:#fff;border-color:var(--rx)}
.dir{font-weight:700;border-radius:4px;padding:1px 6px;font-size:12px;color:#fff}.dir.TX{background:var(--tx)}.dir.RX{background:var(--rx)}
.tag{font-size:11px;color:var(--mut);border:1px solid var(--line);border-radius:4px;padding:0 4px;margin-left:4px}
.unlink td{color:var(--mut)}.mut{color:var(--mut)}footer{color:var(--mut);font-size:12px;text-align:center;padding:16px}
#msg{color:var(--acc);font-size:12px;min-height:16px}
.sum{display:flex;flex-wrap:wrap;gap:6px 16px;font-size:14px;margin:0 0 8px}.sum b{font-size:18px}
.vt{float:right;font-size:12px;font-weight:400}.vt button{border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:6px;padding:2px 8px;cursor:pointer;font:inherit;margin-left:4px}.vt button.on{background:var(--fg);color:var(--bg)}
.tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:10px}
.tile{background:var(--card);border:1px solid var(--line);border-left:6px solid var(--off);border-radius:8px;padding:9px 11px;display:flex;flex-direction:column;gap:5px;min-height:150px}
.tile.on{border-left-color:var(--rx)}.tile.wait{border-left-color:var(--acc)}
.tile .hd{display:flex;justify-content:space-between;align-items:baseline;gap:6px}.tile .nm{font-size:17px;font-weight:700}
.tile .tg{font-size:13px;font-weight:600;white-space:nowrap}.tile .ln{font-size:12px;color:var(--mut)}
.tile .act{border-radius:6px;padding:6px 8px;font-size:13px;background:var(--bg);flex:1}
.tile .act.tx{background:var(--tx);color:#fff}.tile .act.rx{background:var(--rx);color:#fff}
.tile .act.tx .mut,.tile .act.rx .mut{color:rgba(255,255,255,.85)}
.net{font-size:11px;border-radius:4px;padding:0 5px;margin-left:4px;color:#fff;background:var(--mut)}.net.ext{background:#2471a3}.net.int{background:var(--rx)}
.tile .ft{display:flex;justify-content:space-between;align-items:center;font-size:12px;color:var(--mut)}
</style></head><body>
<header><b>DVBridge</b><span><a href="/settings" style="color:#fff;font-size:13px;margin-right:10px">설정</a><small id="clock"></small><button class="th" id="theme" onclick="cycleTheme()" title="화면 테마: 자동(Windows 설정) / 밝게 / 어둡게">자동</button></span></header>
<main>
<div class="stats" id="stats"></div>
<h2>사용자 <span class="vt">보기<button id="vCard" onclick="setView('card')">카드</button><button id="vTable" onclick="setView('table')">표</button></span></h2>
<div class="sum" id="sum"></div>
<div class="tiles" id="tiles"></div>
<div class="tbl" id="tblWrap"><table><thead><tr><th>사용자</th><th>대상</th><th>휴대폰</th><th>현재</th><th title="BM 루프 방지: 60초 안 2초 미만 송신 6회째 차단">짧은 송신</th><th>듣기</th></tr></thead>
<tbody id="users"><tr><td colspan="6">불러오는 중…</td></tr></tbody></table></div>
<div id="msg"></div>
<h2>송수신 이력 (Last Heard)</h2>
<div class="tbl"><table><thead><tr><th>시각</th><th>사용자</th><th>방향</th><th>송신자</th><th>대상</th><th>슬롯</th><th>길이</th></tr></thead>
<tbody id="rows"><tr><td colspan="7">이력 없음</td></tr></tbody></table></div>
<footer>DVBridge — 최초 제작: 최승훈 (6K2KZR) · MIT License · 설정 변경은 [설정]에서 관리자 로그인</footer>
</main>
<script>
const $=id=>document.getElementById(id);
// ---- 화면 테마 (자동 / 밝게 / 어둡게), 브라우저에 기억 ----
const THEMES=['auto','light','dark'],THEME_NAME={auto:'자동',light:'밝게',dark:'어둡게'};
function applyTheme(t){if(t==='auto')document.documentElement.removeAttribute('data-theme');else document.documentElement.setAttribute('data-theme',t);const b=$('theme');if(b)b.textContent='테마: '+THEME_NAME[t];}
function loadTheme(){try{const t=localStorage.getItem('dvbridge-theme');return THEMES.includes(t)?t:'auto'}catch(e){return 'auto'}}
function cycleTheme(){const t=THEMES[(THEMES.indexOf(loadTheme())+1)%3];try{localStorage.setItem('dvbridge-theme',t)}catch(e){}applyTheme(t);}
applyTheme(loadTheme());
function esc(s){return String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function dst(r){if(r.kind==='notice')return '휴대폰';if(r.kind==='intercom')return '내부 채널 '+esc(r.dst);if(r.kind==='echo')return '내부 에코';return r.priv?esc(r.dst)+'<span class="tag">개인</span>':'TG '+esc(r.dst)}
function src(r){if(r.kind==='notice')return '<b>📢 브릿지 공지</b>'+(r.name?' '+esc(r.name):'');if(!r.call)return esc(r.src);return '<b>'+esc(r.call)+'</b>'+(r.name?' ['+esc(r.name)+']':'')+' <span class="mut">('+esc(r.src)+')</span>'}
function t(ms){const d=new Date(ms);return d.toLocaleDateString('ko-KR',{month:'2-digit',day:'2-digit'})+' '+d.toLocaleTimeString('ko-KR',{hour12:false})}
function dur(ms){return (ms/1000).toFixed(1)+'초'}
function who(u){const h=Number(u.hotspot||0);return esc(u.call||'?')+(h>9999999?'·'+String(h%100).padStart(2,'0'):'')}
function upt(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+'일 ':'')+h+'시간 '+m+'분'}
function bar(p){const c=p>=90?'crit':p>=70?'hi':'';return '<div class="bar"><i class="'+c+'" style="width:'+Math.min(100,p)+'%"></i></div>'}
function st(k,v,p){return '<div class="st"><div class="k">'+k+'</div><div class="v">'+v+'</div>'+(p!=null?bar(p):'')+'</div>'}

// ---- 실시간 듣기 ----
let ctx=null,cur=-1,reader=null,nextT=0;
function stopListen(){cur=-1;if(reader){try{reader.cancel()}catch(e){}reader=null}}
async function listen(u){
 if(cur===u){stopListen();render();return;}
 stopListen();
 try{ctx=ctx||new (window.AudioContext||window.webkitAudioContext)();await ctx.resume();}catch(e){$('msg').textContent='이 브라우저는 소리 재생을 지원하지 않습니다';return;}
 cur=u;render();$('msg').textContent='';
 const me=u;
 try{
  const resp=await fetch('/api/listen?u='+u,{cache:'no-store'});
  if(!resp.ok){$('msg').textContent=resp.status===429?'청취자가 너무 많습니다 (사용자당 4명까지)':'듣기를 시작할 수 없습니다';cur=-1;render();return;}
  reader=resp.body.getReader();nextT=0;let carry=null;
  for(;;){
   const {value,done}=await reader.read();
   if(done||cur!==me)break;
   let b=value;
   if(carry){const m=new Uint8Array(carry.length+b.length);m.set(carry);m.set(b,carry.length);b=m;carry=null;}
   if(b.length&1){carry=b.slice(b.length-1);b=b.slice(0,b.length-1);}
   if(!b.length)continue;
   const dv=new DataView(b.buffer,b.byteOffset,b.length),n=b.length/2,f=new Float32Array(n);
   for(let i=0;i<n;i++)f[i]=dv.getInt16(i*2,true)/32768;
   const ab=ctx.createBuffer(1,n,8000);ab.getChannelData(0).set(f);
   const s=ctx.createBufferSource();s.buffer=ab;s.connect(ctx.destination);
   const now=ctx.currentTime;
   if(nextT<now+0.03||nextT>now+1.0)nextT=now+0.2;	// 끊겼다 다시 오면 0.2초 여유를 두고 재생
   s.start(nextT);nextT+=ab.duration;
  }
 }catch(e){if(cur===me)$('msg').textContent='듣기 연결이 끊겼습니다';}
 if(cur===me){cur=-1;render();}
}

let last=null;
function render(){
 if(!last)return;const s=last,us=s.users||[],sv=s.server||{};
 const conn=us.filter(u=>u.connected).length,act=us.filter(u=>u.enabled).length;
 $('stats').innerHTML=
  st('CPU · DVBridge',(sv.proc_cpu??0)+'%',sv.proc_cpu??0)+
  st('CPU · PC 전체',(sv.sys_cpu??0)+'%',sv.sys_cpu??0)+
  st('메모리 · DVBridge',(sv.proc_mem_mb??0)+' MB',null)+
  st('메모리 · PC 전체',(sv.sys_mem_pct??0)+'% / '+(sv.sys_mem_gb??0)+' GB',sv.sys_mem_pct??0)+
  st('가동 시간',upt(sv.uptime_s||0),null)+
  st('접속 사용자',conn+' / '+act,act?conn*100/act:0)+
  st('청취자',(s.listeners??0)+'명',null)+
  st('ID 명단',s.iddb_count?s.iddb_count.toLocaleString()+'명':'없음',null);
 $('users').innerHTML=us.length?us.map((u,i)=>{
  const dc=u.connected?'on':(u.master_state?'wait':'');
  let now='<span class="mut">대기</span>',cls='';
  if(u.active){const r=u.active;cls=r.dir==='TX'?'tx':'rx';
   now='<span class="now '+cls+'">'+(r.kind==='notice'?'공지 ':r.kind==='intercom'?'내부 ':r.kind==='echo'?'에코 ':r.dir==='TX'?(r.kind==='unlink'?'Unlink ':'송신 '):'수신 ')+'</span>'+src(r)+' → '+dst(r)+' <span class="mut">'+dur(r.dur)+'</span>';}
  else if(!u.enabled)now='<span class="mut">사용 안 함</span>';
  else if(!u.master_state)now='<span class="mut">정지</span>';
  else if(!u.connected)now='<span class="mut">'+esc(u.master_state)+'</span>';
  return '<tr class="'+cls+'"><td><span class="dot '+dc+'" title="'+esc(u.master_state||'정지')+'"></span><b>'+who(u)+'</b></td>'+
   '<td>'+dst({dst:u.target,priv:u.target_priv})+' <span class="mut">TS'+esc(u.slot)+'</span></td>'+
   '<td>'+(u.client?'<span title="'+esc(u.client)+'">● 연결</span>'+netTag(u.client_net)+devTag(u):'<span class="mut">—</span>')+'</td>'+
   '<td>'+now+'</td><td>'+(u.short_tx??'-')+' / 5</td>'+
   '<td><button class="ls'+(cur===i?' on':'')+'" onclick="listen('+i+')">'+(cur===i?'■ 끄기':'🔊 듣기')+'</button></td></tr>';
 }).join(''):'<tr><td colspan="6">사용자가 없습니다 ([설정]에서 사용자 추가)</td></tr>';
 renderTiles(us);
}
// ---- 한눈에 보기: 요약 + 사용자 카드 ----
let hist=[];
function setView(v){try{localStorage.setItem('dvbridge-view',v)}catch(e){}applyView();}
function view(){try{return localStorage.getItem('dvbridge-view')==='table'?'table':'card'}catch(e){return 'card'}}
function applyView(){const v=view();$('tiles').style.display=v==='card'?'':'none';$('tblWrap').style.display=v==='table'?'':'none';$('vCard').className=v==='card'?'on':'';$('vTable').className=v==='table'?'on':'';}
function devTag(u){const c=u.client_info;if(!c)return '';const t=[c.device,c.call].filter(x=>x).join(' · ');return t?' <span class="mut" title="'+esc(c.app||'')+'">'+esc(t)+'</span>':''}
function netTag(k){return k?'<span class="net '+(k==='외부망'?'ext':k==='내부망'?'int':'')+'">'+esc(k)+'</span>':''}
function ago(ms){const s=Math.max(0,Math.round((Date.now()-ms)/1000));return s<60?s+'초 전':s<3600?Math.floor(s/60)+'분 전':s<86400?Math.floor(s/3600)+'시간 전':Math.floor(s/86400)+'일 전'}
function renderTiles(us){
 const on=us.filter(u=>u.enabled).length,conn=us.filter(u=>u.connected).length,ph=us.filter(u=>u.client).length,phx=us.filter(u=>u.client_net==='외부망').length;
 const tx=us.filter(u=>u.active&&u.active.dir==='TX').length,rx=us.filter(u=>u.active&&u.active.dir==='RX').length;
 $('sum').innerHTML='<span>서비스 <b style="color:'+(last.service_on?'var(--rx)':'var(--mut)')+'">'+(last.service_on?'켜짐':'꺼짐')+'</b></span><span>사용자 <b>'+us.length+'</b></span><span>BM 접속 <b style="color:var(--rx)">'+conn+'</b>/'+on+'</span><span>휴대폰 <b>'+ph+'</b>'+(phx?' (외부망 '+phx+')':'')+'</span><span>송신 중 <b style="color:var(--tx)">'+tx+'</b></span><span>수신 중 <b style="color:var(--rx)">'+rx+'</b></span>';
 $('tiles').innerHTML=us.length?us.map((u,i)=>{
  const k=who(u),cls=u.connected?'on':(u.master_state?'wait':'');
  let act='<span class="mut">대기 중</span>',ac='';
  if(u.active){const r=u.active;ac=r.dir==='TX'?'tx':'rx';
   act='<b>'+(r.kind==='notice'?'공지':r.kind==='intercom'?'내부':r.kind==='echo'?'에코':r.dir==='TX'?(r.kind==='unlink'?'Unlink':'송신'):'수신')+'</b> '+src(r)+'<br>→ '+dst(r)+' <span class="mut">'+dur(r.dur)+'</span>';}
  else{const lh=hist.find(r=>r.via===k);if(lh)act='<span class="mut">마지막: '+(lh.dir==='TX'?'송신':'수신')+' '+(lh.call?esc(lh.call):esc(lh.src))+' → '+(lh.priv?esc(lh.dst):'TG '+esc(lh.dst))+' · '+ago(lh.start)+'</span>';}
  const state=!u.enabled?'사용 안 함':!u.master_state?'정지':esc(u.master_state);
  return '<div class="tile '+cls+'"><div class="hd"><span class="nm">'+k+'</span><span class="tg">'+dst({dst:u.target,priv:u.target_priv})+' · TS'+esc(u.slot)+'</span></div>'+
   '<div class="ln">BM: <span style="color:'+(u.connected?'var(--rx)':'var(--mut)')+'">'+state+'</span> · 휴대폰: '+(u.client?'<span style="color:var(--rx)" title="'+esc(u.client)+'">● 연결</span>'+netTag(u.client_net)+devTag(u):'—')+' · USRP '+esc(u.usrp_port)+'</div>'+
   '<div class="act '+ac+'">'+act+'</div>'+
   '<div class="ft"><span title="BM 루프 방지: 60초 안 2초 미만 송신 6회째 차단">짧은 송신 '+(u.short_tx??'-')+'/5</span><button class="ls'+(cur===i?' on':'')+'" onclick="listen('+i+')">'+(cur===i?'■ 끄기':'🔊 듣기')+'</button></div></div>';
 }).join(''):'<div class="mut">사용자가 없습니다 — [설정]에서 사용자를 추가하세요</div>';
}
applyView();
async function tick(){
 $('clock').textContent=new Date().toLocaleTimeString('ko-KR',{hour12:false});
 try{
  last=await (await fetch('/api/status',{cache:'no-store'})).json();render();
  const h=await (await fetch('/api/history',{cache:'no-store'})).json();
  hist=h;
  $('rows').innerHTML=h.length?h.map(r=>'<tr class="'+(r.kind==='unlink'?'unlink':'')+'"><td>'+t(r.start)+'</td><td>'+esc(r.via||'')+'</td><td><span class="dir '+r.dir+'">'+r.dir+'</span>'+(r.kind==='unlink'?'<span class="tag">Unlink</span>':r.kind==='notice'?'<span class="tag">공지</span>':r.kind==='intercom'?'<span class="tag">내부</span>':r.kind==='echo'?'<span class="tag">에코</span>':'')+'</td><td>'+src(r)+'</td><td>'+dst(r)+'</td><td>TS'+esc(r.slot)+'</td><td>'+dur(r.dur)+'</td></tr>').join(''):'<tr><td colspan="7">이력 없음</td></tr>';
 }catch(e){$('users').innerHTML='<tr><td colspan="6">DVBridge에 연결할 수 없음</td></tr>';}
}
tick();setInterval(tick,1000);
</script></body></html>)HTML");
}
