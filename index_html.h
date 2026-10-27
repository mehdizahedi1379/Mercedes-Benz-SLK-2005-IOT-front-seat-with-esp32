#pragma once
// New UI for the ESP32 controller.
// Self-contained: no CDN / internet needed (works on the ESP32's own AP too).
// Uses the same endpoints as before, plus one new optional endpoint: /state

const char* index_html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#16323a">
<title>ESP32 Control</title>
<style>
:root{
  --bg:#e9eef0; --panel:#ffffff; --ink:#14262b; --muted:#5d7278; --line:#d3dde0;
  --head:#16323a; --head-ink:#eaf4f6;
  --teal:#0f7c8a; --teal-d:#0a5f6a; --blue:#2f5fb3; --blue-d:#234a8f;
  --ok:#1f8a4c; --warn:#c77700; --bad:#c62f2f; --bad-bg:#fbe9e9; --warn-bg:#fff2dc; --ok-bg:#e4f4ea;
  --radius:14px;
}
@media (prefers-color-scheme:dark){
  :root{ --bg:#0f1b1f; --panel:#18282d; --ink:#e4eef0; --muted:#8fa5ab; --line:#2a3f45;
    --head:#0b1417; --teal:#2bb3c4; --teal-d:#1f8f9e; --blue:#6f9cf0; --blue-d:#5584d8;
    --ok:#4fd08a; --warn:#f0a437; --bad:#ff7b7b; --bad-bg:#3a1f22; --warn-bg:#3a2b12; --ok-bg:#17301f; }
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0}
body{background:var(--bg);color:var(--ink);font:16px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
  padding-bottom:env(safe-area-inset-bottom,0)}
button,input{font:inherit;color:inherit}
:focus-visible{outline:3px solid var(--teal);outline-offset:2px}

/* header with live current */
header{position:sticky;top:0;z-index:10;background:var(--head);color:var(--head-ink);
  padding:calc(12px + env(safe-area-inset-top,0)) 16px 14px}
.hwrap{max-width:720px;margin:0 auto}
.htop{display:flex;align-items:center;justify-content:space-between;gap:12px}
h1{font-size:1.05rem;margin:0;font-weight:650;letter-spacing:.01em}
.conn{display:flex;align-items:center;gap:8px;font-size:.85rem;opacity:.9}
.dot{width:10px;height:10px;border-radius:50%;background:#e25b5b}
.conn.on .dot{background:#46d17f}
.read{display:flex;align-items:baseline;gap:8px;margin-top:8px}
.amps{font-size:2.6rem;font-weight:700;font-variant-numeric:tabular-nums;line-height:1}
.unit{font-size:1.1rem;opacity:.75}
.meter{height:8px;border-radius:6px;background:rgba(255,255,255,.16);margin-top:10px;overflow:hidden}
.meter>i{display:block;height:100%;width:0;background:#46d1c0;transition:width .35s}
.meter.hot>i{background:#ff8a5c}

main{max-width:720px;margin:0 auto;padding:16px;display:grid;gap:14px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);padding:16px}
.card h2{font-size:1rem;margin:0 0 12px;font-weight:650}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px}

/* hold-to-move buttons */
.drive{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.hold{min-height:68px;border:0;border-radius:12px;background:var(--blue);color:#fff;font-weight:650;font-size:1.05rem;
  touch-action:none;user-select:none;-webkit-user-select:none;cursor:pointer;
  box-shadow:0 3px 0 var(--blue-d);transition:transform .06s,box-shadow .06s}
.hold.r{background:var(--teal);box-shadow:0 3px 0 var(--teal-d)}
.hold.down{transform:translateY(3px);box-shadow:0 0 0 transparent;filter:brightness(1.12)}
.hold:disabled{opacity:.5}
.hint{font-size:.8rem;color:var(--muted);margin:8px 0 0}

/* brake */
.brake{margin-top:14px;padding-top:14px;border-top:1px solid var(--line)}
.chip{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border-radius:999px;font-size:.82rem;font-weight:600;
  background:var(--ok-bg);color:var(--ok)}
.chip.bad{background:var(--bad-bg);color:var(--bad)}
.chip.warn{background:var(--warn-bg);color:var(--warn)}
.stepper{display:flex;align-items:stretch;border:1px solid var(--line);border-radius:10px;overflow:hidden}
.stepper button{width:44px;border:0;background:transparent;font-size:1.3rem;cursor:pointer}
.stepper button:active{background:var(--line)}
.stepper input{width:72px;border:0;border-inline:1px solid var(--line);text-align:center;background:transparent;
  font-variant-numeric:tabular-nums;font-weight:600;-moz-appearance:textfield}
.stepper input::-webkit-outer-spin-button,.stepper input::-webkit-inner-spin-button{-webkit-appearance:none;margin:0}
.btn{border:1px solid var(--line);background:transparent;border-radius:10px;padding:10px 14px;cursor:pointer;font-weight:600}
.btn:active{background:var(--line)}
.btn.pri{background:var(--head);border-color:var(--head);color:var(--head-ink)}
.btn.warn{border-color:var(--warn);color:var(--warn)}
.btn.ok{border-color:var(--ok);color:var(--ok)}
.set-row{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin-top:12px}
.lbl{font-size:.9rem;color:var(--muted)}

/* segmented LED control + switch */
.seg{display:grid;grid-template-columns:repeat(4,1fr);border:1px solid var(--line);border-radius:12px;overflow:hidden}
.seg button{border:0;background:transparent;padding:14px 4px;cursor:pointer;font-weight:600;border-inline-start:1px solid var(--line)}
.seg button:first-child{border-inline-start:0}
.seg button[aria-pressed=true]{background:var(--teal);color:#fff}
.heater{width:100%;min-height:56px;border-radius:12px;border:1px solid var(--line);background:transparent;cursor:pointer;
  display:flex;align-items:center;justify-content:space-between;padding:0 16px;font-weight:650}
.heater .sw{width:46px;height:26px;border-radius:999px;background:var(--line);position:relative;transition:background .2s}
.heater .sw::after{content:"";position:absolute;top:3px;left:3px;width:20px;height:20px;border-radius:50%;background:#fff;transition:transform .2s}
.heater[aria-pressed=true] .sw{background:var(--bad)}
.heater[aria-pressed=true] .sw::after{transform:translateX(20px)}

footer{text-align:center;color:var(--muted);font-size:.85rem;padding:4px 0 20px}
footer b{color:var(--ink);font-variant-numeric:tabular-nums}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}
@media (min-width:640px){ .secs{display:grid;grid-template-columns:1fr 1fr;gap:14px} }
.secs{display:grid;gap:14px}
</style>
</head>
<body>

<header>
  <div class="hwrap">
    <div class="htop">
      <h1>ESP32 Controller</h1>
      <div id="conn" class="conn"><span class="dot"></span><span id="connTxt">Connecting…</span></div>
    </div>
    <div class="read"><span class="amps" id="amps">0.00</span><span class="unit">A</span></div>
    <div class="meter" id="meter"><i id="meterBar"></i></div>
  </div>
</header>

<main>
  <div class="secs" id="secs"></div>

  <section class="card">
    <h2>LED brightness</h2>
    <div class="seg" id="ledSeg">
      <button data-b="off" aria-pressed="true">Off</button>
      <button data-b="low" aria-pressed="false">Low</button>
      <button data-b="medium" aria-pressed="false">Medium</button>
      <button data-b="high" aria-pressed="false">High</button>
    </div>
  </section>

  <section class="card">
    <h2>Heater</h2>
    <button class="heater" id="heaterBtn" aria-pressed="false">
      <span id="heaterTxt">Heater is off</span><span class="sw"></span>
    </button>
  </section>

  <footer>Address: <b id="ip">…</b></footer>
</main>

<script>
(function(){
  var SECTIONS = [1,2,3,4];
  var held = {};            // currently held buttons, so we can release them all on blur
  var online = false;

  // ---------- build section cards ----------
  var secs = document.getElementById('secs');
  SECTIONS.forEach(function(n){
    var c = document.createElement('section');
    c.className = 'card';
    c.innerHTML =
      '<div class="row"><h2 style="margin:0">Section '+n+'</h2><span class="chip" id="chip'+n+'">Brake armed</span></div>'+
      '<div class="drive" style="margin-top:12px">'+
        '<button class="hold" data-s="'+n+'" data-d="left">◀ Left</button>'+
        '<button class="hold r" data-s="'+n+'" data-d="right">Right ▶</button>'+
      '</div>'+
      '<p class="hint">Hold to move, release to stop.</p>'+
      '<div class="brake">'+
        '<div class="lbl">Over-current limit</div>'+
        '<div class="set-row">'+
          '<div class="stepper">'+
            '<button type="button" data-step="-0.1" data-s="'+n+'" aria-label="Decrease limit">−</button>'+
            '<input type="number" id="thr'+n+'" step="0.1" min="0" inputmode="decimal" aria-label="Limit in amps for section '+n+'">'+
            '<button type="button" data-step="0.1" data-s="'+n+'" aria-label="Increase limit">+</button>'+
          '</div>'+
          '<span class="lbl">A</span>'+
          '<button class="btn pri" data-save="'+n+'">Save limit</button>'+
        '</div>'+
        '<div class="set-row"><button class="btn warn" id="tog'+n+'" data-tog="'+n+'">Disable brake</button></div>'+
      '</div>';
    secs.appendChild(c);
  });

  // ---------- helpers ----------
  function send(path){
    return fetch(path).then(function(r){ if(!r.ok) throw new Error(r.status); return r; });
  }
  function setOnline(v){
    if(v===online) return; online=v;
    var c=document.getElementById('conn');
    c.classList.toggle('on',v);
    document.getElementById('connTxt').textContent = v?'Connected':'Connection lost';
  }

  // ---------- hold-to-move (pointer events, releases safely) ----------
  function move(s,d,on){
    send('/control?section='+s+'&direction='+d+'&action='+(on?'on':'off')).catch(function(){});
  }
  function release(btn){
    var k = btn.dataset.s+btn.dataset.d;
    if(!held[k]) return;
    delete held[k];
    btn.classList.remove('down');
    move(btn.dataset.s, btn.dataset.d, false);
  }
  document.querySelectorAll('.hold').forEach(function(btn){
    btn.addEventListener('pointerdown',function(e){
      e.preventDefault();
      btn.setPointerCapture(e.pointerId);
      var k = btn.dataset.s+btn.dataset.d;
      if(held[k]) return;
      held[k]=btn; btn.classList.add('down');
      move(btn.dataset.s, btn.dataset.d, true);
    });
    ['pointerup','pointercancel','lostpointercapture'].forEach(function(ev){
      btn.addEventListener(ev,function(){ release(btn); });
    });
    btn.addEventListener('contextmenu',function(e){ e.preventDefault(); });
  });
  function releaseAll(){ Object.keys(held).forEach(function(k){ release(held[k]); }); }
  window.addEventListener('blur',releaseAll);
  document.addEventListener('visibilitychange',function(){ if(document.hidden) releaseAll(); });

  // ---------- brake limit controls ----------
  function thr(n){ return document.getElementById('thr'+n); }
  document.querySelectorAll('[data-step]').forEach(function(b){
    b.addEventListener('click',function(){
      var i=thr(b.dataset.s), v=parseFloat(i.value)||0;
      i.value=Math.max(0,v+parseFloat(b.dataset.step)).toFixed(1);
    });
  });
  document.querySelectorAll('[data-save]').forEach(function(b){
    b.addEventListener('click',function(){
      var n=b.dataset.save, v=parseFloat(thr(n).value);
      if(isNaN(v)||v<0){ thr(n).focus(); return; }
      send('/setBrake?section='+n+'&value='+v).then(function(){
        b.textContent='Saved'; setTimeout(function(){ b.textContent='Save limit'; },1200);
      }).catch(function(){ b.textContent='Failed'; setTimeout(function(){ b.textContent='Save limit'; },1500); });
    });
  });
  document.querySelectorAll('[data-tog]').forEach(function(b){
    b.addEventListener('click',function(){
      send('/toggleInactiveBrake?section='+b.dataset.tog).then(pollOnce).catch(function(){});
    });
  });

  // ---------- LED + heater ----------
  var segBtns=document.querySelectorAll('#ledSeg button');
  function showLed(name){ segBtns.forEach(function(b){ b.setAttribute('aria-pressed', b.dataset.b===name?'true':'false'); }); }
  segBtns.forEach(function(b){
    b.addEventListener('click',function(){
      send('/led?brightness='+b.dataset.b).then(function(){ showLed(b.dataset.b); }).catch(function(){});
    });
  });
  var heaterBtn=document.getElementById('heaterBtn');
  function showHeater(on){
    heaterBtn.setAttribute('aria-pressed',on?'true':'false');
    document.getElementById('heaterTxt').textContent = on?'Heater is on':'Heater is off';
  }
  heaterBtn.addEventListener('click',function(){
    send('/heater?action=toggle').then(function(){ return fetch('/state'); })
      .then(function(r){ return r.text(); }).then(applyState).catch(function(){});
  });
  function applyState(t){            // format from /state: "<brightness 0-255>,<heater 0|1>"
    var p=t.trim().split(','); if(p.length<2) return;
    var b=parseInt(p[0],10);
    showLed(b===0?'off':b<=50?'low':b<=150?'medium':'high');
    showHeater(p[1]==='1');
  }

  // ---------- polling (one request at a time, the ESP32 is single-threaded) ----------
  var MAX_A = 10;
  function text(url){ return fetch(url).then(function(r){ if(!r.ok) throw new Error(r.status); return r.text(); }); }

  function updateBrakeUi(n,state){
    var chip=document.getElementById('chip'+n), tog=document.getElementById('tog'+n);
    chip.className='chip';
    if(state==='active'){ chip.classList.add('bad'); chip.textContent='Over-current: stopped';
      tog.textContent='Disable brake'; tog.className='btn warn'; }
    else if(state==='inactive_manual'){ chip.classList.add('warn'); chip.textContent='Brake disabled';
      tog.textContent='Enable brake'; tog.className='btn ok'; }
    else { chip.textContent='Brake armed'; tog.textContent='Disable brake'; tog.className='btn warn'; }
  }

  function pollOnce(){
    return text('/current').then(function(t){
      var a=parseFloat(t)||0;
      document.getElementById('amps').textContent=a.toFixed(2);
      var pct=Math.min(100,a/MAX_A*100);
      document.getElementById('meterBar').style.width=pct+'%';
      document.getElementById('meter').classList.toggle('hot',pct>70);
      setOnline(true);
      return SECTIONS.reduce(function(p,n){
        return p.then(function(){ return text('/getBrakeStatus?section='+n).then(function(s){ updateBrakeUi(n,s.trim()); }); });
      },Promise.resolve());
    }).catch(function(){ setOnline(false); });
  }

  function loadThresholds(){
    return SECTIONS.reduce(function(p,n){
      return p.then(function(){ return text('/getBrakeThreshold?section='+n).then(function(v){
        if(document.activeElement!==thr(n)) thr(n).value=parseFloat(v).toFixed(1);
      }); });
    },Promise.resolve()).catch(function(){});
  }

  function loop(){ pollOnce().then(function(){ setTimeout(loop,online?800:2000); }); }
  var slow=0;
  function slowLoop(){
    text('/ip').then(function(t){ document.getElementById('ip').textContent=t; }).catch(function(){})
      .then(function(){ return text('/state').then(applyState).catch(function(){}); })
      .then(function(){ setTimeout(slowLoop,4000); });
  }

  loadThresholds();
  loop();
  slowLoop();
})();
</script>
</body>
</html>
)rawliteral";
