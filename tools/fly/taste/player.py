#!/usr/bin/env python3
"""A one-page player for a taste round: every fly plays the same moment, one key switches between them.

  python3 tools/fly/taste/player.py NN <renders dir> <out.html>

Takes the round's renders (roundNN-L.wav), keeps the first 8 passes and the last 8 passes of the loop
(the start, and the fly after it has learned), and embeds them as MP3 in one HTML file. Needs ffmpeg.
"""
import base64, json, os, subprocess, sys, tempfile, wave

HERE = os.path.dirname(os.path.abspath(__file__))

PAGE = r'''<title>__TITLE__</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Urbanist:wght@400;600&family=Fragment+Mono&display=swap">
<style>
/* layout: one column on true black; a row of big fly buttons, one timeline under them, the answer at the bottom */
:root {
  color-scheme: dark;
  --bg: #000000; --panel: #0d0c0c; --line: #262322; --fg: #e9e4df; --dim: #8b847e;
  --itch: #b3241b; --yes: #cfc8c0; --no: #4a4542;
  --words: "Urbanist", system-ui, sans-serif; --numbers: "Fragment Mono", ui-monospace, monospace;
}
html, body { height: 100%; }
body { background: var(--bg); color: var(--fg); font-family: var(--words); font-size: 16px; line-height: 1.5;
  padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); padding-block: 28px; }
main { max-width: 760px; margin: 0 auto; display: flex; flex-direction: column; gap: 22px; }
h1 { font-size: 22px; font-weight: 600; margin: 0; letter-spacing: 0.01em; text-wrap: balance; }
p { margin: 0; color: var(--dim); max-width: 62ch; }
kbd { font-family: var(--numbers); font-size: 13px; color: var(--fg); border: 1px solid var(--line); border-radius: 4px; padding: 0 5px; }
.flies { display: grid; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); gap: 10px; }
.fly { display: flex; flex-direction: column; gap: 10px; background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 12px; min-width: 0; }
.fly.on { border-color: var(--itch); }
.pick { font: 600 40px/1 var(--words); color: var(--dim); background: none; border: 0; padding: 14px 0; cursor: pointer; border-radius: 6px; }
.fly.on .pick { color: var(--itch); }
.pick:focus-visible, .rate button:focus-visible, .bar:focus-visible, .go:focus-visible { outline: 2px solid var(--fg); outline-offset: 2px; }
.cap { font-size: 13px; color: var(--dim); text-align: center; min-height: 2.6em; line-height: 1.3; }
.cap:empty { display: none; }
.rate { display: grid; grid-template-columns: repeat(3, 1fr); gap: 4px; }
.rate button { font: 400 13px/1 var(--words); color: var(--dim); background: none; border: 1px solid var(--line); border-radius: 5px; padding: 8px 0; cursor: pointer; }
.rate button[aria-pressed="true"] { color: var(--bg); background: var(--yes); border-color: var(--yes); }
.rate button.no[aria-pressed="true"] { color: var(--fg); background: var(--no); border-color: var(--no); }
.transport { display: flex; align-items: center; gap: 14px; }
.go { font: 600 15px/1 var(--words); color: var(--bg); background: var(--fg); border: 0; border-radius: 999px; padding: 12px 20px; cursor: pointer; min-width: 92px; }
.time { font-family: var(--numbers); font-size: 13px; color: var(--dim); font-variant-numeric: tabular-nums; white-space: nowrap; }
.bar { position: relative; flex: 1; height: 34px; cursor: pointer; border-radius: 4px; min-width: 0; }
.bar::before { content: ""; position: absolute; left: 0; right: 0; top: 16px; height: 2px; background: var(--line); }
.bar .half { position: absolute; left: 50%; top: 8px; bottom: 8px; width: 1px; background: var(--dim); }
.bar .head { position: absolute; top: 9px; width: 2px; height: 16px; background: var(--itch); left: 0; }
.legend { display: flex; justify-content: space-between; font-size: 13px; color: var(--dim); margin-left: 106px; margin-right: 90px; }
.answer { display: flex; flex-direction: column; gap: 10px; border-top: 1px solid var(--line); padding-top: 18px; }
.answer output { font-family: var(--numbers); font-size: 14px; color: var(--fg); background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 12px; user-select: all; overflow-wrap: anywhere; }
.answer .row { display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }
@media (max-width: 480px) { .legend { margin-left: 0; margin-right: 0; } .transport { flex-wrap: wrap; } .bar { flex-basis: 100%; order: 3; } }
</style>
<main>
  <h1>__HEAD__</h1>
  <p>__COUNT__ flies play the same minute. Press play, then switch with <kbd>1</kbd> to <kbd>__COUNT__</kbd> or by clicking a letter. The switch lands on the same spot in the loop, so you hear only the fly change. <kbd>space</kbd> stops and starts.</p>
  <div class="flies" id="flies"></div>
  <div class="transport">
    <button class="go" id="go" type="button">play</button>
    <div class="bar" id="bar" tabindex="0" role="slider" aria-label="position" aria-valuemin="0" aria-valuemax="100" aria-valuenow="0"><span class="half"></span><span class="head" id="head"></span></div>
    <span class="time" id="time">0:00 / 1:00</span>
  </div>
  <div class="legend"><span>a newborn fly, first 30 s</span><span>the same fly after a minute, last 30 s</span></div>
  <div class="answer">
    <p>Mark each fly, then copy the line and paste it into the thread.</p>
    <output id="out">__ROUND__: nothing marked yet</output>
    <div class="row"><button class="go" id="copy" type="button">copy</button><span class="time" id="copied"></span></div>
  </div>
</main>
<script>
const ROUND = '__ROUND__', CLIPS = __CLIPS__, CAPS = __CAPS__;
const letters = Object.keys(CLIPS), marks = {};
let ctx = null, buffers = {}, gains = {}, sources = [], playing = false, startedAt = 0, offset = 0, current = letters[0], length = 60;
const $ = id => document.getElementById(id);
const flies = $('flies');
letters.forEach((L, i) => {
  const d = document.createElement('div'); d.className = 'fly' + (i === 0 ? ' on' : ''); d.id = 'fly-' + L;
  d.innerHTML = `<button class="pick" type="button" id="pick-${L}" aria-label="listen to fly ${L}">${L}</button><span class="cap">${CAPS[L] || ''}</span>
    <div class="rate"><button type="button" id="yes-${L}" aria-pressed="false">yes</button><button type="button" id="ok-${L}" aria-pressed="false">ok</button><button type="button" class="no" id="no-${L}" aria-pressed="false">no</button></div>`;
  flies.appendChild(d);
  $('pick-' + L).onclick = () => choose(L);
  for (const m of ['yes', 'ok', 'no']) $(m + '-' + L).onclick = () => mark(L, m);
});
function mark(L, m) {
  marks[L] = marks[L] === m ? undefined : m;
  for (const k of ['yes', 'ok', 'no']) $(k + '-' + L).setAttribute('aria-pressed', String(marks[L] === k));
  const parts = letters.filter(x => marks[x]).map(x => x + ' ' + marks[x]);
  $('out').textContent = ROUND + ': ' + (parts.length ? parts.join(', ') : 'nothing marked yet');
  $('copied').textContent = '';
}
async function load() {
  ctx = new (window.AudioContext || window.webkitAudioContext)();
  for (const L of letters) {
    const bin = atob(CLIPS[L]), bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    buffers[L] = await ctx.decodeAudioData(bytes.buffer);
    gains[L] = ctx.createGain(); gains[L].gain.value = L === current ? 1 : 0; gains[L].connect(ctx.destination);
  }
  length = Math.min(...letters.map(L => buffers[L].duration));
}
function position() { return playing ? (offset + ctx.currentTime - startedAt) % length : offset; }
function start() {
  sources = letters.map(L => { const s = ctx.createBufferSource(); s.buffer = buffers[L]; s.loop = true; s.loopEnd = length; s.connect(gains[L]); return s; });
  startedAt = ctx.currentTime + 0.03; sources.forEach(s => s.start(startedAt, offset)); playing = true; $('go').textContent = 'stop';
}
function stop() { offset = position(); sources.forEach(s => { try { s.stop(); } catch (e) {} }); sources = []; playing = false; $('go').textContent = 'play'; }
async function toggle() {
  if (!ctx) { $('go').textContent = 'loading'; await load(); }
  if (ctx.state === 'suspended') await ctx.resume();
  playing ? stop() : start();
}
function choose(L) {
  current = L;
  letters.forEach(x => { $('fly-' + x).classList.toggle('on', x === L); if (ctx) gains[x].gain.setTargetAtTime(x === L ? 1 : 0, ctx.currentTime, 0.012); });
  if (ctx && !playing) toggle(); else if (!ctx) toggle();
}
function seek(frac) { const was = playing; if (was) stop(); offset = Math.max(0, Math.min(0.999, frac)) * length; if (was) start(); draw(); }
function draw() {
  const p = ctx ? position() : 0, f = p / length, fmt = s => Math.floor(s / 60) + ':' + String(Math.floor(s % 60)).padStart(2, '0');
  $('head').style.left = (f * 100) + '%'; $('time').textContent = fmt(p) + ' / ' + fmt(length); $('bar').setAttribute('aria-valuenow', Math.round(f * 100));
}
(function loop() { draw(); requestAnimationFrame(loop); })();
$('go').onclick = toggle;
$('bar').onclick = e => { const r = $('bar').getBoundingClientRect(); if (ctx) seek((e.clientX - r.left) / r.width); };
$('bar').onkeydown = e => { if (!ctx) return; if (e.key === 'ArrowRight') seek(position() / length + 0.05); if (e.key === 'ArrowLeft') seek(position() / length - 0.05); };
document.addEventListener('keydown', e => {
  if (e.target.closest && e.target.closest('.bar')) return;
  const n = parseInt(e.key, 10);
  if (n >= 1 && n <= letters.length) { choose(letters[n - 1]); e.preventDefault(); }
  else if (letters.includes(e.key.toUpperCase()) && !e.ctrlKey && !e.metaKey) choose(e.key.toUpperCase());
  else if (e.key === ' ') { toggle(); e.preventDefault(); }
});
$('copy').onclick = async () => {
  const text = $('out').textContent;
  try { await navigator.clipboard.writeText(text); $('copied').textContent = 'copied'; }
  catch (e) { const r = document.createRange(); r.selectNodeContents($('out')); const s = getSelection(); s.removeAllRanges(); s.addRange(r); $('copied').textContent = 'selected: press ctrl+c'; }
};
</script>
'''


def build(files, period, n, out, title, head, tag, caps=None):
    """files: {letter: wav}. Keeps the first 8 and last 8 of n passes (each `period` seconds)."""
    clips = {}
    with tempfile.TemporaryDirectory() as tmp:
        for L in sorted(files):
            src = files[L]
            mp3 = os.path.join(tmp, L + '.mp3')
            f = (f'[0:a]atrim=start=0:end={8 * period},asetpts=PTS-STARTPTS[a];'
                 f'[0:a]atrim=start={(n - 8) * period}:end={n * period},asetpts=PTS-STARTPTS[b];[a][b]concat=n=2:v=0:a=1')
            subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', src, '-filter_complex', f, '-c:a', 'libmp3lame', '-b:a', '160k', mp3], check=True)
            clips[L] = base64.b64encode(open(mp3, 'rb').read()).decode()
    page = (PAGE.replace('__ROUND__', tag).replace('__TITLE__', title).replace('__HEAD__', head).replace('__COUNT__', str(len(clips)))
            .replace('__CAPS__', json.dumps(caps or {})).replace('__CLIPS__', json.dumps(clips)))
    open(out, 'w').write(page)
    print(out, round(len(page) / 1e6, 1), 'MB')


def main(number, renders, out):
    number = int(number)
    r = json.load(open(os.path.join(HERE, 'rounds', f'{number:02d}.json')))
    w = wave.open(os.path.join(HERE, 'loops', r['loop']))
    files = {L: os.path.join(renders, f'round{number:02d}-{L}.wav') for L in r['variants']}
    build(files, w.getnframes() / w.getframerate(), r['repeat'], out, f'Fly Taste Round {number}',
          f'the fly, taste round {number}', f'round {number}')


if __name__ == '__main__':
    main(*sys.argv[1:4])
