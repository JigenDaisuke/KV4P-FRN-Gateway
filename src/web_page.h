// Single-page web UI (served from flash). Data comes from /api/status and
// /api/config as JSON; settings are posted to /save. Dark/light/auto theme.
#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="it"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>FRN ITALIA Gateway</title>
<style>
:root{--bg:#0d1512;--card:#14201b;--card2:#1b2a24;--tx:#e6f0eb;--mu:#8fa69b;--ac:#00c46a;--ac2:#00e57c;--bd:#24372f;--red:#ff5d5d;--yel:#ffc845;--blu:#4fb3ff}
[data-t=light]{--bg:#f2f5f3;--card:#fff;--card2:#eef3f0;--tx:#13201a;--mu:#5d7067;--bd:#d6e0db;--ac:#00a259;--ac2:#00b864}
@media(prefers-color-scheme:light){[data-t=auto]{--bg:#f2f5f3;--card:#fff;--card2:#eef3f0;--tx:#13201a;--mu:#5d7067;--bd:#d6e0db;--ac:#00a259;--ac2:#00b864}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{display:flex;align-items:center;gap:12px;padding:12px 16px;background:var(--card);border-bottom:1px solid var(--bd);position:sticky;top:0;z-index:5}
header h1{font-size:17px;margin:0;flex:1}header h1 small{display:block;color:var(--mu);font-weight:400;font-size:12px}
.logo{width:40px;height:40px}
button,.btn{background:var(--card2);color:var(--tx);border:1px solid var(--bd);border-radius:9px;padding:8px 14px;font:inherit;cursor:pointer;text-decoration:none}
button.pri{background:var(--ac);border-color:var(--ac);color:#fff;font-weight:600}button:hover{filter:brightness(1.1)}
nav{display:flex;gap:4px;overflow-x:auto;padding:8px 12px;background:var(--card);border-bottom:1px solid var(--bd)}
nav a{padding:7px 13px;border-radius:8px;color:var(--mu);text-decoration:none;white-space:nowrap;cursor:pointer}nav a.on{background:var(--card2);color:var(--ac2);font-weight:600}
main{max-width:980px;margin:0 auto;padding:14px 14px 90px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:14px;padding:16px;margin-bottom:14px}
.card h2{font-size:14px;text-transform:uppercase;letter-spacing:.06em;color:var(--mu);margin:0 0 12px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:12px}
label{display:flex;flex-direction:column;gap:4px;font-size:13px;color:var(--mu)}
input,select{background:var(--bg);color:var(--tx);border:1px solid var(--bd);border-radius:8px;padding:8px 10px;font:inherit}
input:focus,select:focus{outline:2px solid var(--ac);border-color:transparent}
label.ck{flex-direction:row;align-items:center;gap:8px;color:var(--tx)}label.ck input{width:18px;height:18px;accent-color:var(--ac)}
.hint{font-size:12px;color:var(--mu);margin:8px 0 0}
.chips{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:14px}
.chip{display:flex;align-items:center;gap:7px;background:var(--card);border:1px solid var(--bd);border-radius:20px;padding:6px 12px;font-size:13px}
.dot{width:10px;height:10px;border-radius:50%;background:var(--mu)}.ok{background:var(--ac2);box-shadow:0 0 8px var(--ac2)}.warn{background:var(--yel)}.bad{background:var(--red);box-shadow:0 0 8px var(--red)}.txc{background:var(--red);box-shadow:0 0 10px var(--red)}.rxc{background:var(--blu);box-shadow:0 0 10px var(--blu)}
.big{font-size:26px;font-weight:700;margin:2px 0}.mu{color:var(--mu);font-size:13px}
.meter{height:10px;background:var(--bg);border-radius:6px;overflow:hidden;border:1px solid var(--bd)}.meter i{display:block;height:100%;width:0;background:linear-gradient(90deg,var(--ac),var(--yel) 75%,var(--red));transition:width .25s}
.kv{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}.kv div{background:var(--card2);border-radius:10px;padding:8px 10px}.kv b{display:block;font-size:16px}
pre{background:#060b09;color:#9ff0c3;border-radius:10px;padding:10px;max-height:320px;overflow:auto;font-size:12px;margin:0;white-space:pre-wrap}
.bar{position:fixed;left:0;right:0;bottom:0;background:var(--card);border-top:1px solid var(--bd);padding:10px 16px;display:flex;gap:10px;justify-content:flex-end;align-items:center;z-index:5}
.bar span{flex:1;color:var(--mu);font-size:13px}.tab{display:none}.tab.on{display:block}
.tg{display:flex;gap:8px;align-items:end}.tg label{flex:1;min-width:0}input,select{min-width:0;width:100%}label.ck input{width:18px}
a{color:var(--ac2)}
</style></head><body data-t="auto">
<header><img class="logo" src="/logo.png" alt="FRN ITALIA"><h1>FRN ITALIA &mdash; Gateway KV4P<small id="hv">...</small></h1>
<button id="thm" title="Cambia tema">Tema</button></header>
<nav id="nav"><a data-p="st" class="on">Stato</a><a data-p="frn">Server FRN</a><a data-p="el">EchoLink</a><a data-p="rad">Radio</a><a data-p="gw">Gateway</a><a data-p="ton">Toni</a><a data-p="dtmf">DTMF</a><a data-p="wifi">Wi-Fi</a><a data-p="sys">Sistema</a></nav>
<main><form id="f">
<section class="tab on" id="st">
 <div class="chips"><span class="chip"><i class="dot" id="cW"></i><span id="tW">Wi-Fi</span></span><span class="chip"><i class="dot" id="cF"></i><span id="tF">Rete</span></span><span class="chip"><i class="dot" id="cR"></i>Radio</span><span class="chip"><i class="dot" id="cT"></i>PTT</span><span class="chip"><i class="dot" id="cS"></i>Segnale RX</span></div>
 <div class="card"><h2>Gateway</h2><div class="big" id="gs">-</div><div class="mu" id="gt">&nbsp;</div>
  <p class="mu" style="margin:12px 0 4px">Livello audio dalla radio <span id="lv"></span></p><div class="meter"><i id="lm"></i></div></div>
 <div class="card"><h2>Statistiche</h2><div class="kv" id="kv"></div></div>
 <div class="card"><h2>Log</h2><pre id="lg"></pre></div>
</section>
<section class="tab" id="frn"><div class="card"><h2>Server FRN</h2><div class="grid">
 <label>Server<input name="frnServer"></label><label>Porta<input name="frnPort" type="number"></label>
 <label>Server di backup<input name="frnBkServer" placeholder="vuoto = quello del server"></label><label>Porta backup<input name="frnBkPort" type="number"></label>
 <label>Stanza (net)<input name="frnNet"></label><label>Versione client<input name="frnVersion"></label></div></div>
 <div class="card"><h2>Account e stazione</h2><div class="grid">
 <label>E-mail<input name="frnEmail" type="email"></label><label>Password FRN<input name="frnPassword" type="password" data-s="frnPassword_set"></label>
 <label>Nominativo<input name="frnCallsign" style="text-transform:uppercase"></label><label>Nome operatore<input name="frnName"></label>
 <label>Citt&agrave; - zona<input name="frnCity" placeholder="Roma - Centro"></label><label>Nazione<input name="frnCountry"></label>
 <label>Descrizione<input name="frnDescr"></label>
 <label>Tipo stazione<select name="frnCl"><option value="1">Gateway</option><option value="0">Crosslink</option><option value="2">PC Only</option></select></label>
 <label>Banda / canale (BC)<input name="frnBC" id="bc"></label></div>
 <p class="hint" id="bch"></p><p class="hint">Nominativo, nome, citt&agrave; e nazione sono obbligatori: senza, il server rifiuta il collegamento.</p></div></section>
<section class="tab" id="el"><div class="card"><h2>EchoLink</h2><div class="grid">
 <label>Sistema all'accensione<select name="sysStart"><option value="0">FRN</option><option value="1">EchoLink</option></select></label>
 <label>Nominativo del nodo<input name="elCall" placeholder="IU8UUT-L" style="text-transform:uppercase"></label>
 <label>Password EchoLink<input name="elPass" type="password" data-s="elPass_set"></label>
 <label>Nome (sysop)<input name="elSysop"></label>
 <label>Localit&agrave; (max 27 caratteri)<input name="elLoc" maxlength="27" placeholder="Napoli 145.500 FM"></label>
 <label>Server directory<input name="elServer"></label>
 <label class="ck"><input type="checkbox" name="elAccept">Accetta chiamate in arrivo</label>
 <label>Chiudi dopo inattivit&agrave; (min, 0 = mai)<input name="elIdle" type="number" min="0" max="240"></label></div>
 <label style="margin-top:12px">Messaggio informativo inviato a chi si collega<input name="elInfo"></label>
 <p class="hint">Il gateway &egrave; su <b>un sistema alla volta</b>. Dalla radio: <b>#*1</b> = FRN, <b>#*2</b> = EchoLink. In EchoLink: <b>#</b> + numero nodo = chiama il nodo (es. <b>#9999</b> = test), <b>#0</b> = chiudi il collegamento. Il nominativo -L (o -R) va validato da EchoLink a parte: il nominativo semplice non &egrave; ammesso su un nodo collegato a una radio.</p></div>
 <div class="card"><h2>Rete</h2>
 <label class="ck" style="margin-bottom:12px"><input type="checkbox" name="elPxOn">Usa proxy (senza spunta = UDP diretto)</label><div class="grid">
 <label>Proxy EchoLink<input name="elPxHost" placeholder="elp1.iw9gdc.it, elp4.iw9gdc.it, echoproxy39.ec5w.es"></label>
 <label>Porta proxy<input name="elPxPort" type="number"></label>
 <label>Password proxy<input name="elPxPass"></label></div>
 <p class="hint">Senza la spunta il gateway usa le porte UDP 5198 e 5199: per ricevere chiamate devi inoltrarle dal router all'IP del gateway. Con un proxy (es. in rete mobile) non serve aprire porte; per i proxy pubblici la password &egrave; PUBLIC. Puoi scrivere <b>pi&ugrave; proxy separati da virgola</b>: se uno &egrave; occupato il gateway passa da solo al successivo. Elenco aggiornato: <a href="https://www.echolink.org/proxylist.jsp" target="_blank">echolink.org/proxylist.jsp</a>.</p></div></section>
<section class="tab" id="rad"><div class="card"><h2>Modulo radio</h2><div class="grid">
 <label>Modulo<select name="uhf"><option value="0">VHF 134-174 MHz</option><option value="1">UHF 400-480 MHz</option></select></label>
 <label>Frequenza RX (MHz)<input name="freqRx" type="number" step="0.00625"></label><label>Frequenza TX (MHz)<input name="freqTx" type="number" step="0.00625"></label>
 <label>Canalizzazione<select name="bw"><option value="0">12,5 kHz</option><option value="1">25 kHz</option></select></label>
 <label>CTCSS in TX<select name="ctcssTx" class="ct"></select></label><label>CTCSS in RX (tono d'accesso)<select name="ctcssRx" class="ct"></select></label>
 <label>Squelch (1-8)<input name="squelch" type="number" min="1" max="8"></label><label>Volume uscita modulo (1-8)<input name="volume" type="number" min="1" max="8"></label></div>
 <div class="grid" style="margin-top:12px"><label class="ck"><input type="checkbox" name="fEmph">Enfasi / de-enfasi</label><label class="ck"><input type="checkbox" name="fHp">Passa-alto 300 Hz</label><label class="ck"><input type="checkbox" name="fLp">Passa-basso 3 kHz</label></div></div>
 <div class="card"><h2>Potenza di trasmissione</h2><div class="grid">
 <label>Potenza<select name="hiPow"><option value="1">Alta (~1 W)</option><option value="0">Bassa (~0,5 W)</option></select></label>
 <label>Collegamento pin H/L del modulo<select name="powPin"><option value="0">Non collegato (KV4P v1.x di serie)</option><option value="1">GPIO23 con filo diretto (modifica v1.x)</option><option value="2">GPIO23 tramite transistor (KV4P v2.0)</option></select></label></div>
 <p class="hint">Sulla KV4P v1.x il pin H/L del modulo non &egrave; collegato all'ESP32: il modulo lavora sempre in potenza alta. Per regolarla collega con un filo il pin H/L del modulo al GPIO23 e scegli "filo diretto".</p></div></section>
<section class="tab" id="gw"><div class="card"><h2>Rilevamento segnale</h2><div class="grid">
 <label>Metodo<select name="sqMode"><option value="0">Pin SQ del modulo</option><option value="1">Livello audio (VOX)</option><option value="2">Entrambi</option></select></label>
 <label>Soglia VOX (RMS)<input name="voxThr" type="number"></label><label>Ritardo apertura (ms)<input name="sqOpenMs" type="number"></label><label>Coda squelch (ms)<input name="sqHangMs" type="number"></label></div></div>
 <div class="card"><h2>Tempi di trasmissione</h2><div class="grid">
 <label>Ritardo PTT &rarr; audio (ms)<input name="txDelayMs" type="number"></label><label>Coda dopo l'audio (ms)<input name="txTailMs" type="number"></label>
 <label>Blocco RX dopo TX (ms)<input name="rxLockMs" type="number"></label><label>Buffer anti-jitter (ms)<input name="jitterMs" type="number"></label>
 <label>Time-out (s)<input name="totSec" type="number"></label></div></div>
 <div class="card"><h2>Livelli audio</h2><div class="grid">
 <label>Guadagno radio &rarr; FRN<input name="rxGain" type="number" step="0.05"></label><label>Guadagno FRN &rarr; radio<input name="txGain" type="number" step="0.05"></label></div></div></section>
<section class="tab" id="ton"><div class="card"><h2>Toni di cortesia (coda)</h2>
 <div class="tg"><label>Fine QSO ricevuto da FRN<select name="tQso" class="tp"></select></label><label>Nota (Hz)<input name="tQsoHz" type="number"></label><button type="button" data-t="1">Prova</button></div><br>
 <div class="tg"><label>Gateway impegnato (fine della mia trasmissione)<select name="tLoc" class="tp"></select></label><label>Nota (Hz)<input name="tLocHz" type="number"></label><button type="button" data-t="2">Prova</button></div><br>
 <div class="tg"><label>Gateway NON impegnato (rete occupata / scollegato)<select name="tBusy" class="tp"></select></label><label>Nota (Hz)<input name="tBusyHz" type="number"></label><button type="button" data-t="3">Prova</button></div><br>
 <div class="grid"><label>Livello toni<input name="tLevel" type="number"></label></div>
 <p class="hint">"Gateway impegnato" suona solo se la tua voce &egrave; davvero uscita su FRN; "non impegnato" se il gateway non ha avuto la parola. Il pulsante Prova trasmette in radio con le impostazioni salvate.</p></div>
 <div class="card"><h2>Identificativo CW</h2><div class="grid">
 <label class="ck"><input type="checkbox" name="cwId">Invia il nominativo in CW</label><label>Al massimo ogni (min)<input name="cwIdMin" type="number"></label>
 <label>Velocit&agrave; (WPM)<input name="cwWpm" type="number"></label><label>Nota (Hz)<input name="cwTone" type="number"></label><label>Livello<input name="cwLevel" type="number"></label></div></div></section>
<section class="tab" id="dtmf"><div class="card"><h2>Cambio stanza con i toni DTMF</h2>
 <div class="grid"><label class="ck"><input type="checkbox" name="dtmfOn">Comandi DTMF attivi</label>
 <label>Ritorno alla stanza principale dopo (min, 0 = mai)<input name="dtmfRet" type="number" min="0" max="240"></label></div>
 <p class="hint">Dalla radio invia <b>#</b> seguito dal codice (es. <b>#2</b>) e rilascia il PTT: il gateway conferma con tre toni e il codice in CW, poi entra nella stanza. Codice sconosciuto = tono basso discendente. <b>#0</b> riporta sempre alla stanza principale (scheda Server FRN). I toni non vengono inoltrati in rete.</p>
 <div id="dt" class="grid" style="margin-top:12px;grid-template-columns:repeat(auto-fill,minmax(300px,1fr))"></div></div></section>
<section class="tab" id="wifi"><div class="card"><h2>Reti Wi-Fi (in ordine di priorit&agrave;)</h2>
 <div class="grid"><label>Rete 1 - SSID<input name="wifiSsid0"></label><label>Password<input name="wifiPass0" type="password" data-s="wifiPass0_set"></label></div><br>
 <div class="grid"><label>Rete 2 - SSID<input name="wifiSsid1"></label><label>Password<input name="wifiPass1" type="password" data-s="wifiPass1_set"></label></div><br>
 <div class="grid"><label>Rete 3 - SSID<input name="wifiSsid2"></label><label>Password<input name="wifiPass2" type="password" data-s="wifiPass2_set"></label></div>
 <p class="hint">All'avvio, e quando la rete cade, il gateway si collega alla prima rete della lista che trova attiva. Solo reti a 2,4 GHz.</p></div>
 <div class="card"><h2>Accesso</h2><div class="grid"><label>Nome host (mDNS)<input name="hostname"></label><label>Password pagina (utente admin)<input name="webPass" type="password" data-s="webPass_set"></label></div></div></section>
<section class="tab" id="sys"><div class="card"><h2>Aggiornamento firmware</h2><input type="file" id="fw" accept=".bin"> <button type="button" id="up">Carica</button><p class="hint" id="ups"></p></div>
 <div class="card"><h2>Dispositivo</h2><button type="button" id="rb">Riavvia</button><p class="hint">Comunit&agrave; FRN ITALIA &mdash; <a href="https://t.me/frnitalia" target="_blank">gruppo Telegram</a> &middot; <a href="https://github.com/JigenDaisuke/KV4P-FRN-Gateway" target="_blank">progetto su GitHub</a></p></div></section>
</form></main>
<div class="bar"><span id="msg"></span><button class="pri" id="sv">Salva e riavvia</button></div>
<script>
const $=s=>document.querySelector(s),$$=s=>document.querySelectorAll(s),F=$('#f');
const CT=[0,67,71.9,74.4,77,79.7,82.5,85.4,88.5,91.5,94.8,97.4,100,103.5,107.2,110.9,114.8,118.8,123,127.3,131.8,136.5,141.3,146.2,151.4,156.7,162.2,167.9,173.8,179.9,186.2,192.8,203.5,210.7,218.1,225.7,233.6,241.8,250.3];
const TP=['Nessuno','Beep corto','Beep doppio','Bip-bop (sale)','Bop-bip (scende)','Tre toni','Trillo','K in CW','R in CW (roger)'];
$$('.ct').forEach(s=>CT.forEach((v,i)=>s.add(new Option(i?v.toFixed(1)+' Hz':'nessuno',i))));
$$('.tp').forEach(s=>TP.forEach((v,i)=>s.add(new Option(v,i))));
for(let i=0;i<10;i++)$('#dt').insertAdjacentHTML('beforeend','<div class="tg"><label style="flex:0 0 90px">Codice<input name="dCode'+i+'" inputmode="numeric" pattern="[0-9]*" placeholder="#"></label><label>Stanza<input name="dRoom'+i+'"></label></div>');
const TN={auto:'Tema: auto',dark:'Tema: scuro',light:'Tema: chiaro'};let th='auto';try{th=localStorage.getItem('th')||'auto'}catch(e){}
function thSet(){document.body.dataset.t=th;$('#thm').textContent=TN[th]}thSet();
$('#thm').onclick=()=>{th={auto:'dark',dark:'light',light:'auto'}[th];thSet();try{localStorage.setItem('th',th)}catch(e){}};
function msg(t){$('#msg').textContent=t}
$$('#nav a').forEach(a=>a.onclick=()=>{$$('#nav a').forEach(x=>x.classList.toggle('on',x==a));$$('.tab').forEach(t=>t.classList.toggle('on',t.id==a.dataset.p));$('.bar').style.display=(a.dataset.p=='st'||a.dataset.p=='sys')?'none':'flex'});
$('.bar').style.display='none';
let C={};
function bcUpd(){const t=F.frnCl.value;$('#bc').disabled=t!='1';$('#bch').textContent=t=='1'?('Vuoto = automatico: '+(C.bcAuto||'')):('Per questo tipo il server riceve "'+(t=='0'?'Crosslink':'PC Only')+'"')}
F.frnCl.onchange=bcUpd;
fetch('/api/config').then(r=>r.json()).then(j=>{C=j;for(const e of F.elements){if(!e.name||!(e.name in j))continue;const v=j[e.name];if(e.type=='checkbox')e.checked=!!v;else if(e.type=='password'){}else e.value=(typeof v=='boolean')?(v?1:0):v}
 $$('input[data-s]').forEach(e=>e.placeholder=j[e.dataset.s]?'(impostata - vuoto = non cambiare)':'(non impostata)');$('#hv').textContent='firmware v'+j.version;bcUpd()});
$('#sv').onclick=()=>{if(!confirm('Salvare le impostazioni e riavviare il gateway?'))return;const b=new URLSearchParams();for(const e of F.elements){if(!e.name||e.disabled)continue;if(e.type=='checkbox'){if(e.checked)b.append(e.name,'1')}else b.append(e.name,e.value)}
 fetch('/save',{method:'POST',body:b}).then(r=>{msg(r.ok?'Salvato: riavvio in corso, ricarica tra 10 secondi...':'Errore nel salvataggio');if(r.ok)setTimeout(()=>location.reload(),10000)})};
$$('button[data-t]').forEach(b=>b.onclick=()=>fetch('/api/test?t='+b.dataset.t,{method:'POST'}).then(()=>msg('Tono di prova inviato')));
$('#rb').onclick=()=>{if(confirm('Riavviare il gateway?'))fetch('/reboot',{method:'POST'}).then(()=>{msg('Riavvio...');setTimeout(()=>location.reload(),10000)})};
$('#up').onclick=()=>{const f=$('#fw').files[0];if(!f)return;const d=new FormData();d.append('fw',f);const x=new XMLHttpRequest();x.open('POST','/update');
 x.upload.onprogress=e=>$('#ups').textContent='Caricamento '+Math.round(e.loaded*100/e.total)+'%';x.onload=()=>{$('#ups').textContent=x.status==200?'Aggiornato, riavvio...':'Errore';if(x.status==200)setTimeout(()=>location.reload(),15000)};x.send(d)};
function dot(id,c){$(id).className='dot '+c}
const GW={'IDLE':['In ascolto','ok'],'RADIO->FRN':['Radio &rarr; ',''],'FRN->RADIO':['',' &rarr; Radio'],'LOCKOUT':['Pausa','warn']};
function st(){fetch('/api/status').then(r=>r.json()).then(j=>{
 dot('#cW',j.wifi?'ok':'bad');$('#tW').textContent=j.wifi?(j.ssid+' '+j.rssi+' dBm'):'Wi-Fi assente';
 const on=['IDLE','TX_WAITING','TX_AUDIO','RX_AUDIO','RX_LIST','RX_CLIENT_LIST','RX_CLIENT_LIST_HEADER','TX_APPROVED','ONLINE','CONNECTED','CONNECTING'].includes(j.frn);dot('#cF',on?'ok':(j.frn=='LOGIN_REJECTED'?'bad':'warn'));
 $('#tF').textContent=j.sys+' '+(j.frn=='CONNECTED'?('con '+j.elRemote):(on?(j.frn=='CONNECTING'?'chiamata...':'collegato'):j.frn.toLowerCase()));
 dot('#cR',j.radio?'ok':'bad');dot('#cT',j.ptt?'txc':'');dot('#cS',j.carrier?'rxc':'');
 const g=GW[j.gw]||[j.gw,''];$('#gs').innerHTML=j.gw=='RADIO->FRN'?g[0]+j.sys:(j.gw=='FRN->RADIO'?j.sys+g[1]:g[0]);
 $('#gt').textContent=j.gw=='FRN->RADIO'?('Parla: '+j.talker):(j.gw=='RADIO->FRN'?('Stai trasmettendo su '+j.sys):('Ultimo da radio: '+(j.lastLocal||'-')));
 $('#lv').textContent=j.level;$('#lm').style.width=Math.min(100,j.level/120)+'%';
 const k={'Sistema':j.sys,'Stazioni in rete':j.clients,'Stanza / nodo':j.sys=='FRN'?j.net:(j.elRemote||'-'),'Pacchetti RX':j.rx,'Pacchetti TX':j.tx,'Login / connessioni':j.logins+' / '+j.conn,'Frame GSM errati':j.bad,'Clock ADC':j.adc+' Hz','IP':j.ip,'Uptime':Math.floor(j.up/60)+' h '+(j.up%60)+' min','Memoria libera':Math.round(j.heap/1024)+' kB'};
 $('#kv').innerHTML=Object.entries(k).map(([a,b])=>'<div><span class=mu>'+a+'</span><b></b></div>').join('');let i=0;for(const v of Object.values(k))$$('#kv b')[i++].textContent=v;
}).catch(()=>{dot('#cW','bad');$('#tW').textContent='Gateway non raggiungibile'})}
function lg(){if(!$('#st').classList.contains('on'))return;fetch('/log').then(r=>r.text()).then(t=>{const p=$('#lg'),b=p.scrollTop+p.clientHeight>=p.scrollHeight-20;p.textContent=t;if(b)p.scrollTop=p.scrollHeight})}
st();lg();setInterval(st,1000);setInterval(lg,2500);
</script></body></html>)HTML";
