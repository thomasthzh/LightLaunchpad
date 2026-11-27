(() => {
  const DURATION = 16.5;
  const CHAPTERS = [
    { id: 0, start: 0.0, end: 2.6, label: '01 · Alt+D 唤醒' },
    { id: 1, start: 2.6, end: 5.8, label: '02 · 液态玻璃聚焦' },
    { id: 2, start: 5.8, end: 8.8, label: '03 · 光标下方随行' },
    { id: 3, start: 8.8, end: 11.8, label: '04 · 剪贴板与热词' },
    { id: 4, start: 11.8, end: 14.4, label: '05 · 热力图与时长' },
    { id: 5, start: 14.4, end: 16.5, label: '06 · 原生单文件' }
  ];

  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const lerp = (a, b, t) => a + (b - a) * t;
  const invLerp = (a, b, v) => clamp((v - a) / (b - a), 0, 1);
  const easeOutExpo = t => (t >= 1 ? 1 : 1 - Math.pow(2, -10 * t));
  const easeInOutCubic = t => (t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2);
  const easeOutBack = t => {
    const c1 = 1.35, c3 = c1 + 1;
    return 1 + c3 * Math.pow(t - 1, 3) + c1 * Math.pow(t - 1, 2);
  };

  function rrect(ctx, x, y, w, h, r) {
    const rad = Math.min(r, w * 0.5, h * 0.5);
    ctx.beginPath();
    ctx.moveTo(x + rad, y);
    ctx.arcTo(x + w, y, x + w, y + h, rad);
    ctx.arcTo(x + w, y + h, x, y + h, rad);
    ctx.arcTo(x, y + h, x, y, rad);
    ctx.arcTo(x, y, x + w, y, rad);
    ctx.closePath();
  }

  // Synthesized subtle Web Audio foley (muted by default until user clicks unmute)
  class FoleyEngine {
    constructor() {
      this.ctx = null;
      this.muted = true;
      this.lastCue = -1;
    }
    setMuted(m) {
      this.muted = m;
      if (!m && !this.ctx) {
        const AC = window.AudioContext || window.webkitAudioContext;
        if (AC) this.ctx = new AC();
      }
      if (!m && this.ctx && this.ctx.state === 'suspended') this.ctx.resume();
    }
    trigger(t) {
      if (this.muted || !this.ctx) return;
      const cues = [
        { id: 1, at: 1.12, type: 'click' },
        { id: 2, at: 2.65, type: 'swell' },
        { id: 3, at: 6.85, type: 'ping' },
        { id: 4, at: 9.0, type: 'chime' },
        { id: 5, at: 14.5, type: 'finale' }
      ];
      if (t < 0.2) this.lastCue = -1;
      for (const c of cues) {
        if (t >= c.at && t < c.at + 0.14 && this.lastCue !== c.id) {
          this.lastCue = c.id;
          this.play(c.type);
        }
      }
    }
    play(type) {
      const now = this.ctx.currentTime;
      const osc = this.ctx.createOscillator();
      const gain = this.ctx.createGain();
      osc.connect(gain);
      gain.connect(this.ctx.destination);
      if (type === 'click') {
        osc.type = 'triangle';
        osc.frequency.setValueAtTime(320, now);
        osc.frequency.exponentialRampToValueAtTime(68, now + 0.065);
        gain.gain.setValueAtTime(0.18, now);
        gain.gain.exponentialRampToValueAtTime(0.001, now + 0.07);
        osc.start(now); osc.stop(now + 0.075);
      } else if (type === 'swell') {
        osc.type = 'sine';
        osc.frequency.setValueAtTime(92, now);
        osc.frequency.exponentialRampToValueAtTime(148, now + 0.45);
        gain.gain.setValueAtTime(0.001, now);
        gain.gain.linearRampToValueAtTime(0.14, now + 0.14);
        gain.gain.exponentialRampToValueAtTime(0.001, now + 0.65);
        osc.start(now); osc.stop(now + 0.68);
      } else if (type === 'ping') {
        osc.type = 'sine';
        osc.frequency.setValueAtTime(528, now);
        osc.frequency.exponentialRampToValueAtTime(792, now + 0.18);
        gain.gain.setValueAtTime(0.09, now);
        gain.gain.exponentialRampToValueAtTime(0.001, now + 0.24);
        osc.start(now); osc.stop(now + 0.26);
      } else if (type === 'chime') {
        osc.type = 'sine';
        osc.frequency.setValueAtTime(440, now);
        osc.frequency.exponentialRampToValueAtTime(660, now + 0.28);
        gain.gain.setValueAtTime(0.08, now);
        gain.gain.exponentialRampToValueAtTime(0.001, now + 0.36);
        osc.start(now); osc.stop(now + 0.38);
      } else {
        osc.type = 'sine';
        osc.frequency.setValueAtTime(220, now);
        osc.frequency.exponentialRampToValueAtTime(440, now + 0.65);
        gain.gain.setValueAtTime(0.001, now);
        gain.gain.linearRampToValueAtTime(0.12, now + 0.2);
        gain.gain.exponentialRampToValueAtTime(0.001, now + 0.9);
        osc.start(now); osc.stop(now + 0.95);
      }
    }
  }

  // Atmospheric twilight alpine backdrop (rendered once to offscreen canvas for 60fps performance)
  function createBackdrop(w, h) {
    const off = document.createElement('canvas');
    off.width = w; off.height = h;
    const c = off.getContext('2d');

    const sky = c.createLinearGradient(0, 0, w * 0.85, h);
    sky.addColorStop(0, '#070a12');
    sky.addColorStop(0.38, '#0f1b33');
    sky.addColorStop(0.72, '#1a2744');
    sky.addColorStop(1, '#090d16');
    c.fillStyle = sky;
    c.fillRect(0, 0, w, h);

    // Subtle atmospheric aurora ribbons
    const glow1 = c.createRadialGradient(w * 0.28, h * 0.32, 10, w * 0.28, h * 0.32, w * 0.42);
    glow1.addColorStop(0, 'rgba(56, 189, 248, 0.26)');
    glow1.addColorStop(0.55, 'rgba(59, 130, 246, 0.08)');
    glow1.addColorStop(1, 'rgba(0,0,0,0)');
    c.fillStyle = glow1;
    c.fillRect(0, 0, w, h);

    const glow2 = c.createRadialGradient(w * 0.76, h * 0.64, 10, w * 0.76, h * 0.64, w * 0.38);
    glow2.addColorStop(0, 'rgba(139, 92, 246, 0.22)');
    glow2.addColorStop(0.55, 'rgba(244, 114, 182, 0.06)');
    glow2.addColorStop(1, 'rgba(0,0,0,0)');
    c.fillStyle = glow2;
    c.fillRect(0, 0, w, h);

    // Layered alpine silhouettes for crisp refractive contrast behind Liquid Glass
    const drawRidge = (pts, fill) => {
      c.beginPath();
      c.moveTo(0, h);
      for (const [px, py] of pts) c.lineTo(px * w, py * h);
      c.lineTo(w, h);
      c.closePath();
      c.fillStyle = fill;
      c.fill();
    };
    drawRidge([[0, 0.72], [0.18, 0.56], [0.36, 0.66], [0.54, 0.44], [0.74, 0.60], [0.90, 0.49], [1, 0.58]], 'rgba(20, 32, 58, 0.82)');
    drawRidge([[0, 0.80], [0.22, 0.66], [0.44, 0.76], [0.64, 0.58], [0.84, 0.72], [1, 0.65]], 'rgba(12, 19, 36, 0.92)');
    drawRidge([[0, 0.89], [0.30, 0.78], [0.58, 0.86], [0.82, 0.76], [1, 0.84]], '#070b14');

    // Fine horizon light streak
    c.strokeStyle = 'rgba(148, 197, 255, 0.14)';
    c.lineWidth = 1.5;
    c.beginPath();
    c.moveTo(w * 0.12, h * 0.56);
    c.quadraticCurveTo(w * 0.52, h * 0.41, w * 0.88, h * 0.54);
    c.stroke();

    return off;
  }

  function drawLiquidGlassPanel(ctx, x, y, w, h, r, backdrop, W, H, opts = {}) {
    const rim = opts.rim ?? 1.0;
    const tint = opts.tint ?? 0.08;
    const scaleRefract = opts.refract ?? 1.045;
    const glow = opts.glow ?? 1.0;

    ctx.save();
    // Soft grounded shadow
    ctx.shadowColor = 'rgba(0, 0, 0, 0.58)';
    ctx.shadowBlur = 46;
    ctx.shadowOffsetY = 24;
    rrect(ctx, x, y, w, h, r);
    ctx.fillStyle = 'rgba(12, 16, 24, 0.35)';
    ctx.fill();
    ctx.restore();

    // Clipped crystal interior with subtle meniscus optical refraction
    ctx.save();
    rrect(ctx, x, y, w, h, r);
    ctx.clip();

    if (backdrop) {
      // Crystal clear inner backdrop
      ctx.drawImage(backdrop, 0, 0, W, H);
      // Curved meniscus edge refraction band
      const band = 26;
      ctx.save();
      ctx.beginPath();
      ctx.rect(x, y, w, h);
      rrect(ctx, x + band, y + band, w - band * 2, h - band * 2, Math.max(4, r - 10));
      ctx.clip('evenodd');
      ctx.translate(x + w * 0.5, y + h * 0.5);
      ctx.scale(scaleRefract, scaleRefract);
      ctx.translate(-(x + w * 0.5), -(y + h * 0.5));
      ctx.globalAlpha = 0.72;
      ctx.drawImage(backdrop, 0, 0, W, H);
      ctx.restore();
    }

    // Ultra-subtle cool surface tint
    const surfGrad = ctx.createLinearGradient(x, y, x + w, y + h);
    surfGrad.addColorStop(0, `rgba(255, 255, 255, ${tint * 1.4})`);
    surfGrad.addColorStop(0.5, `rgba(18, 24, 38, ${tint * 3.8})`);
    surfGrad.addColorStop(1, `rgba(147, 197, 253, ${tint * 1.1})`);
    ctx.fillStyle = surfGrad;
    ctx.fillRect(x, y, w, h);

    // Top inner specular sheen
    const sheen = ctx.createLinearGradient(x, y, x, y + h * 0.32);
    sheen.addColorStop(0, `rgba(255, 255, 255, ${0.14 * glow})`);
    sheen.addColorStop(1, 'rgba(255, 255, 255, 0)');
    ctx.fillStyle = sheen;
    ctx.fillRect(x, y, w, h * 0.32);
    ctx.restore();

    // Dual-angle diagonal specular rim (Apple Liquid Glass signature!)
    ctx.save();
    rrect(ctx, x + 0.75, y + 0.75, w - 1.5, h - 1.5, r);
    const rimGrad = ctx.createLinearGradient(x, y, x + w, y + h);
    rimGrad.addColorStop(0, `rgba(255, 255, 255, ${0.72 * rim})`);
    rimGrad.addColorStop(0.24, `rgba(186, 230, 253, ${0.26 * rim})`);
    rimGrad.addColorStop(0.50, `rgba(255, 255, 255, ${0.06 * rim})`);
    rimGrad.addColorStop(0.78, `rgba(147, 197, 253, ${0.24 * rim})`);
    rimGrad.addColorStop(1, `rgba(255, 255, 255, ${0.58 * rim})`);
    ctx.strokeStyle = rimGrad;
    ctx.lineWidth = 1.6;
    ctx.stroke();
    ctx.restore();
  }

  function drawCaption(ctx, W, H, eyebrow, headline, alpha, align = 'top') {
    if (alpha <= 0.01) return;
    ctx.save();
    ctx.globalAlpha = clamp(alpha, 0, 1);
    const yBase = align === 'top' ? H * 0.115 : H * 0.855;
    ctx.textAlign = 'center';
    if (eyebrow) {
      ctx.font = `600 ${Math.round(W * 0.0115)}px "Segoe UI", "Microsoft YaHei UI", sans-serif`;
      ctx.fillStyle = 'rgba(147, 197, 253, 0.92)';
      ctx.fillText(eyebrow, W * 0.5, yBase - W * 0.022);
    }
    ctx.font = `700 ${Math.round(W * 0.032)}px "Microsoft YaHei UI", "Segoe UI", sans-serif`;
    ctx.fillStyle = '#F8FAFC';
    ctx.shadowColor = 'rgba(0,0,0,0.55)';
    ctx.shadowBlur = 18;
    ctx.fillText(headline, W * 0.5, yBase + W * 0.008);
    ctx.restore();
  }

  function drawKeycap3D(ctx, x, y, w, h, label, pressed, rimSweep) {
    const depth = lerp(16, 4, pressed);
    const yOff = lerp(0, 11, pressed);
    ctx.save();
    // Base shadow
    ctx.shadowColor = 'rgba(0,0,0,0.75)';
    ctx.shadowBlur = 32;
    ctx.shadowOffsetY = 18;
    rrect(ctx, x, y + yOff + depth, w, h, 22);
    ctx.fillStyle = '#090b10';
    ctx.fill();
    ctx.restore();

    // Keycap skirt
    rrect(ctx, x, y + yOff + depth * 0.55, w, h, 22);
    ctx.fillStyle = '#141822';
    ctx.fill();

    // Keycap top deck
    const topG = ctx.createLinearGradient(x, y + yOff, x + w, y + yOff + h);
    topG.addColorStop(0, '#262c3a');
    topG.addColorStop(0.55, '#191d27');
    topG.addColorStop(1, '#11141c');
    rrect(ctx, x, y + yOff, w, h, 20);
    ctx.fillStyle = topG;
    ctx.fill();

    // Directional cold rim light
    const rimG = ctx.createLinearGradient(x + w * (rimSweep - 0.5), y + yOff, x + w * (rimSweep + 0.5), y + yOff + h);
    rimG.addColorStop(0, 'rgba(255,255,255,0.06)');
    rimG.addColorStop(0.5, pressed > 0.3 ? 'rgba(96, 165, 250, 0.95)' : 'rgba(226, 232, 240, 0.65)');
    rimG.addColorStop(1, 'rgba(255,255,255,0.05)');
    rrect(ctx, x + 1, y + yOff + 1, w - 2, h - 2, 19);
    ctx.strokeStyle = rimG;
    ctx.lineWidth = 2;
    ctx.stroke();

    // Crisp engraved legend
    ctx.font = `700 ${Math.round(h * 0.34)}px "Segoe UI", sans-serif`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = pressed > 0.3 ? '#93C5FD' : '#E2E8F0';
    ctx.fillText(label, x + w * 0.5, y + yOff + h * 0.5);
  }

  function drawEmblem2A(ctx, cx, cy, size, glintPhase) {
    const x = cx - size * 0.5;
    const y = cy - size * 0.5;
    const r = size * 0.22;
    ctx.save();
    ctx.shadowColor = 'rgba(59, 130, 246, 0.32)';
    ctx.shadowBlur = size * 0.42;
    ctx.shadowOffsetY = size * 0.12;
    const bg = ctx.createLinearGradient(x, y, x + size, y + size);
    bg.addColorStop(0, '#2a3346');
    bg.addColorStop(0.5, '#181e2c');
    bg.addColorStop(1, '#0e131e');
    rrect(ctx, x, y, size, size, r);
    ctx.fillStyle = bg;
    ctx.fill();
    ctx.restore();

    // Titanium rim
    rrect(ctx, x + 1, y + 1, size - 2, size - 2, r);
    const rg = ctx.createLinearGradient(x, y, x + size, y + size);
    rg.addColorStop(0, 'rgba(255,255,255,0.68)');
    rg.addColorStop(0.5, 'rgba(147,197,253,0.18)');
    rg.addColorStop(1, 'rgba(255,255,255,0.45)');
    ctx.strokeStyle = rg;
    ctx.lineWidth = Math.max(1.5, size * 0.018);
    ctx.stroke();

    // 3x3 frosted launchpad grid
    const pad = size * 0.21;
    const cell = (size - pad * 2) / 3;
    const dot = cell * 0.54;
    for (let row = 0; row < 3; row++) {
      for (let col = 0; col < 3; col++) {
        const dx = x + pad + col * cell + (cell - dot) * 0.5;
        const dy = y + pad + row * cell + (cell - dot) * 0.5;
        rrect(ctx, dx, dy, dot, dot, dot * 0.28);
        ctx.fillStyle = (row === 1 && col === 1) ? 'rgba(96, 165, 250, 0.92)' : 'rgba(226, 232, 240, 0.26)';
        ctx.fill();
      }
    }

    // Crystal-white faceted launch arrow
    ctx.save();
    ctx.beginPath();
    ctx.moveTo(cx + size * 0.22, cy - size * 0.22);
    ctx.lineTo(cx + size * 0.02, cy + size * 0.22);
    ctx.lineTo(cx - size * 0.03, cy + size * 0.03);
    ctx.lineTo(cx - size * 0.22, cy - size * 0.02);
    ctx.closePath();
    ctx.fillStyle = 'rgba(255, 255, 255, 0.95)';
    ctx.shadowColor = 'rgba(147, 197, 253, 0.6)';
    ctx.shadowBlur = 14;
    ctx.fill();
    ctx.restore();

    // Diagonal specular glint sweep
    if (glintPhase >= 0 && glintPhase <= 1) {
      ctx.save();
      rrect(ctx, x, y, size, size, r);
      ctx.clip();
      const gx = lerp(x - size, x + size * 1.6, glintPhase);
      const gg = ctx.createLinearGradient(gx, y, gx + size * 0.45, y + size);
      gg.addColorStop(0, 'rgba(255,255,255,0)');
      gg.addColorStop(0.5, 'rgba(255,255,255,0.34)');
      gg.addColorStop(1, 'rgba(255,255,255,0)');
      ctx.fillStyle = gg;
      ctx.fillRect(x, y, size, size);
      ctx.restore();
    }
  }

  class HeroFilmPlayer {
    constructor(canvas, ui) {
      this.canvas = canvas;
      this.ctx = canvas.getContext('2d');
      this.ui = ui;
      this.t = 0;
      this.playing = true;
      this.lastTs = null;
      this.foley = new FoleyEngine();
      this.backdrop = null;
      this.W = 1280;
      this.H = 720;

      const params = new URLSearchParams(window.location.search);
      const forcedShot = params.get('shot');
      if (forcedShot !== null && !Number.isNaN(parseFloat(forcedShot))) {
        this.t = clamp(parseFloat(forcedShot), 0, DURATION - 0.01);
        this.playing = false;
      } else if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
        this.t = 4.2; // crisp hero Spotlight poster frame
        this.playing = false;
      }

      this.resize();
      window.addEventListener('resize', () => this.resize());
      this.bindControls();
      this.render();
      requestAnimationFrame(ts => this.loop(ts));
    }

    resize() {
      const rect = this.canvas.getBoundingClientRect();
      const dpr = Math.min(window.devicePixelRatio || 1, 2);
      this.W = Math.max(640, Math.round(rect.width * dpr));
      this.H = Math.max(360, Math.round(rect.height * dpr));
      this.canvas.width = this.W;
      this.canvas.height = this.H;
      this.backdrop = createBackdrop(this.W, this.H);
      this.render();
    }

    bindControls() {
      if (this.ui.playBtn) {
        this.ui.playBtn.addEventListener('click', () => {
          this.playing = !this.playing;
          this.lastTs = null;
          this.syncUi();
        });
      }
      if (this.ui.muteBtn) {
        this.ui.muteBtn.addEventListener('click', () => {
          this.foley.setMuted(!this.foley.muted);
          this.syncUi();
        });
      }
      if (this.ui.replayBtn) {
        this.ui.replayBtn.addEventListener('click', () => {
          this.t = 0;
          this.playing = true;
          this.lastTs = null;
          this.syncUi();
        });
      }
      if (this.ui.chapters) {
        this.ui.chapters.forEach((btn, idx) => {
          btn.addEventListener('click', () => {
            this.t = CHAPTERS[idx].start + 0.02;
            this.playing = true;
            this.lastTs = null;
            this.syncUi();
            this.render();
          });
        });
      }
      this.syncUi();
    }

    syncUi() {
      if (this.ui.playBtn) {
        this.ui.playBtn.textContent = this.playing ? '暂停' : '播放';
        this.ui.playBtn.setAttribute('aria-pressed', String(!this.playing));
      }
      if (this.ui.muteBtn) {
        this.ui.muteBtn.textContent = this.foley.muted ? '开启音效' : '静音';
      }
    }

    loop(ts) {
      if (this.playing) {
        if (this.lastTs !== null) {
          const dt = Math.min(0.05, (ts - this.lastTs) / 1000);
          this.t = (this.t + dt) % DURATION;
          this.foley.trigger(this.t);
        }
        this.lastTs = ts;
        this.render();
      } else {
        this.lastTs = null;
      }

      // Update timeline bar & active chapter
      if (this.ui.progressBar) {
        this.ui.progressBar.style.width = `${(this.t / DURATION) * 100}%`;
      }
      if (this.ui.timecode) {
        this.ui.timecode.textContent = `${this.t.toFixed(1)}s / ${DURATION.toFixed(1)}s`;
      }
      if (this.ui.chapters) {
        CHAPTERS.forEach((ch, idx) => {
          const active = this.t >= ch.start && this.t < ch.end;
          this.ui.chapters[idx].classList.toggle('is-active', active);
        });
      }
      requestAnimationFrame(next => this.loop(next));
    }

    render() {
      const ctx = this.ctx, W = this.W, H = this.H, t = this.t;
      ctx.clearRect(0, 0, W, H);

      if (t < 2.6) this.drawShot1Keycap(ctx, W, H, t);
      else if (t < 5.8) this.drawShot2Spotlight(ctx, W, H, t - 2.6);
      else if (t < 8.8) this.drawShot3MultiMonitor(ctx, W, H, t - 5.8);
      else if (t < 11.8) this.drawShot4LaunchpadEverything(ctx, W, H, t - 8.8);
      else if (t < 14.4) this.drawShot5Heatmap(ctx, W, H, t - 11.8);
      else this.drawShot6Finale(ctx, W, H, t - 14.4);
    }

    // SHOT 1: Sculpted Titanium Alt+D Macro Reveal
    drawShot1Keycap(ctx, W, H, lt) {
      ctx.fillStyle = '#05070c';
      ctx.fillRect(0, 0, W, H);

      const press = lt < 1.05 ? 0 : (lt < 1.55 ? easeOutExpo(invLerp(1.05, 1.24, lt)) : 1 - easeInOutCubic(invLerp(1.55, 1.95, lt)));
      const pulse = invLerp(1.12, 2.3, lt);

      // Optical shockwave ring on depress
      if (pulse > 0) {
        const rad = lerp(W * 0.08, W * 0.65, easeOutExpo(pulse));
        ctx.save();
        ctx.beginPath();
        ctx.arc(W * 0.5, H * 0.54, rad, 0, Math.PI * 2);
        ctx.strokeStyle = `rgba(96, 165, 250, ${(1 - pulse) * 0.55})`;
        ctx.lineWidth = lerp(4, 1, pulse);
        ctx.stroke();
        ctx.restore();
      }

      const zoom = lerp(1.08, 1.0, easeOutExpo(invLerp(0, 2.2, lt)));
      ctx.save();
      ctx.translate(W * 0.5, H * 0.54);
      ctx.scale(zoom, zoom);
      ctx.translate(-W * 0.5, -H * 0.54);

      const kw1 = W * 0.165, kw2 = W * 0.135, kh = H * 0.22, gap = W * 0.028;
      const total = kw1 + kw2 + gap;
      const startX = (W - total) * 0.5;
      const startY = H * 0.44;
      const sweep = invLerp(0.1, 2.2, lt);
      drawKeycap3D(ctx, startX, startY, kw1, kh, 'Alt', press, sweep);
      drawKeycap3D(ctx, startX + kw1 + gap, startY, kw2, kh, 'D', press, sweep);
      ctx.restore();

      // Transition refraction line at end of shot 1 -> becomes Spotlight top edge
      const wipe = invLerp(2.15, 2.6, lt);
      if (wipe > 0) {
        const lw = W * 0.54 * easeOutExpo(wipe);
        const grad = ctx.createLinearGradient(W * 0.5 - lw * 0.5, 0, W * 0.5 + lw * 0.5, 0);
        grad.addColorStop(0, 'rgba(147,197,253,0)');
        grad.addColorStop(0.5, 'rgba(255,255,255,0.95)');
        grad.addColorStop(1, 'rgba(147,197,253,0)');
        ctx.fillStyle = grad;
        ctx.fillRect(W * 0.5 - lw * 0.5, H * 0.27, lw, 2.5);
      }

      drawCaption(ctx, W, H, '01 · INSTANT INVOCATION', '一次按键，即刻唤醒', invLerp(0.15, 0.6, lt) * (1 - invLerp(2.1, 2.55, lt)), 'top');
    }

    // SHOT 2: macOS 26 Crystal Liquid Glass Spotlight Reveal
    drawShot2Spotlight(ctx, W, H, lt) {
      ctx.drawImage(this.backdrop, 0, 0, W, H);

      const pop = easeOutBack(invLerp(0.0, 0.55, lt));
      const scale = lerp(0.88, 1.0, clamp(pop, 0, 1.04));
      const pw = W * 0.54, ph = H * 0.49;
      const px = (W - pw) * 0.5;
      const py = H * 0.275;

      ctx.save();
      ctx.translate(W * 0.5, py + ph * 0.5);
      ctx.scale(scale, scale);
      ctx.translate(-W * 0.5, -(py + ph * 0.5));
      ctx.globalAlpha = easeOutExpo(invLerp(0, 0.25, lt));

      drawLiquidGlassPanel(ctx, px, py, pw, ph, 26, this.backdrop, W, H, { rim: 1.0, tint: 0.055, refract: 1.055 });

      // Search Bar inside Liquid Glass (scaled cleanly to canvas width)
      const s = W / 1280;
      const query = 'feishu';
      const typedCount = Math.floor(invLerp(0.55, 1.45, lt) * query.length);
      const shown = query.slice(0, typedCount);

      ctx.beginPath();
      ctx.arc(px + 34 * s, py + 40 * s, 8.5 * s, 0, Math.PI * 2);
      ctx.strokeStyle = 'rgba(255,255,255,0.85)';
      ctx.lineWidth = Math.max(1.4, 2.2 * s);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(px + 40.5 * s, py + 46.5 * s); ctx.lineTo(px + 46.5 * s, py + 52.5 * s); ctx.stroke();

      ctx.font = `500 ${Math.round(28 * s)}px "Segoe UI", "Microsoft YaHei UI", sans-serif`;
      ctx.textAlign = 'left'; ctx.textBaseline = 'middle';
      ctx.fillStyle = '#F8FAFC';
      ctx.fillText(shown || '搜索应用或文件...', px + 60 * s, py + 41 * s);
      if (Math.floor(lt * 3) % 2 === 0 && typedCount > 0) {
        const tw = ctx.measureText(shown).width;
        ctx.fillStyle = '#60A5FA';
        ctx.fillRect(px + 62 * s + tw, py + 28 * s, Math.max(1.5, 2.2 * s), 26 * s);
      }

      // Hairline divider
      const divY = py + 72 * s;
      ctx.fillStyle = 'rgba(255,255,255,0.12)';
      ctx.fillRect(px + 18 * s, divY, pw - 36 * s, 1);

      // Result rows with spring-gliding focus pill
      const rows = [
        { name: '飞书 Feishu', sub: '应用 · Alt+D → Enter 秒开', color: '#38BDF8', glyph: '飞' },
        { name: 'Visual Studio Code', sub: '应用 · 最近使用', color: '#3B82F6', glyph: 'VS' },
        { name: 'LightLaunchpad 设置', sub: '系统 · Ctrl+, 直达', color: '#818CF8', glyph: '设' }
      ];
      const pillRow = lt < 1.5 ? lerp(1, 0, easeOutExpo(invLerp(0.8, 1.4, lt))) : 0;
      const listTop = divY + 12 * s;
      const rowH = (py + ph - listTop - 12 * s) / 3;
      const pillY = listTop + pillRow * rowH;

      // Gliding Liquid Glass Focus Pill
      rrect(ctx, px + 14 * s, pillY, pw - 28 * s, rowH - 6 * s, 13 * s);
      ctx.fillStyle = 'rgba(255, 255, 255, 0.16)';
      ctx.fill();
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.34)';
      ctx.lineWidth = Math.max(1, 1.2 * s);
      ctx.stroke();

      rows.forEach((r, idx) => {
        const ry = listTop + idx * rowH;
        const iconS = Math.max(14, rowH - 22 * s);
        const iy = ry + (rowH - 6 * s - iconS) * 0.5;
        rrect(ctx, px + 26 * s, iy, iconS, iconS, 9 * s);
        ctx.fillStyle = r.color;
        ctx.fill();
        ctx.font = `700 ${Math.round(iconS * 0.44)}px "Microsoft YaHei UI", sans-serif`;
        ctx.textAlign = 'center'; ctx.fillStyle = '#fff';
        ctx.fillText(r.glyph, px + 26 * s + iconS * 0.5, iy + iconS * 0.52);

        ctx.textAlign = 'left';
        ctx.font = `600 ${Math.round(20 * s)}px "Microsoft YaHei UI", sans-serif`;
        ctx.fillStyle = '#F8FAFC';
        ctx.fillText(r.name, px + 38 * s + iconS, ry + rowH * 0.31);
        ctx.font = `400 ${Math.round(14.5 * s)}px "Microsoft YaHei UI", sans-serif`;
        ctx.fillStyle = 'rgba(226,232,240,0.72)';
        ctx.fillText(r.sub, px + 38 * s + iconS, ry + rowH * 0.64);
      });

      // Enter tactile launch ripple at lt ~ 2.25s
      const launch = invLerp(2.15, 2.85, lt);
      if (launch > 0) {
        rrect(ctx, px + 14 * s, pillY, pw - 28 * s, rowH - 6 * s, 13 * s);
        ctx.strokeStyle = `rgba(96, 165, 250, ${(1 - launch) * 0.9})`;
        ctx.lineWidth = lerp(2, 6, launch) * s;
        ctx.stroke();
      }
      ctx.restore();

      const capAlpha = invLerp(0.42, 0.85, lt) * (1 - invLerp(2.75, 3.15, lt));
      drawCaption(ctx, W, H, '02 · MACOS 26 LIQUID GLASS', '通透折射，毫秒聚焦', capAlpha, 'top');
    }

    // SHOT 3: Dual-Monitor 72% Cursor-Aware Placement
    drawShot3MultiMonitor(ctx, W, H, lt) {
      ctx.fillStyle = '#070a12';
      ctx.fillRect(0, 0, W, H);

      const mW = W * 0.39, mH = H * 0.45, gap = W * 0.04;
      const m1x = (W - mW * 2 - gap) * 0.5;
      const m2x = m1x + mW + gap;
      const my = H * 0.30;

      [m1x, m2x].forEach((mx, idx) => {
        rrect(ctx, mx, my, mW, mH, 16);
        ctx.fillStyle = idx === 1 ? '#10192d' : '#0c1220';
        ctx.fill();
        ctx.strokeStyle = idx === 1 ? 'rgba(96, 165, 250, 0.42)' : 'rgba(255,255,255,0.12)';
        ctx.lineWidth = 1.6;
        ctx.stroke();
        ctx.font = `600 ${Math.round(W * 0.011)}px "Segoe UI", sans-serif`;
        ctx.textAlign = 'left'; ctx.fillStyle = 'rgba(148,163,184,0.65)';
        ctx.fillText(idx === 0 ? 'DISPLAY 1' : 'DISPLAY 2 · ACTIVE CURSOR', mx + 14, my + 22);
      });

      // Luminous cursor gliding from Display 1 to Display 2 upper-right corner
      const move = easeInOutCubic(invLerp(0.15, 1.05, lt));
      const curX = lerp(m1x + mW * 0.32, m2x + mW * 0.88, move);
      const curY = lerp(my + mH * 0.62, my + mH * 0.22, move);

      // Spotlight pops on Display 2 directly BELOW the cursor, clamped safely inside Display 2!
      const appear = easeOutBack(invLerp(1.05, 1.55, lt));
      if (appear > 0.01) {
        const sw = mW * 0.46, sh = mH * 0.44;
        const cx2 = m2x + mW * 0.5;
        const targetCx = lerp(cx2, curX, 0.72);
        const pad = 16;
        const sx = clamp(targetCx - sw * 0.5, m2x + pad, m2x + mW - sw - pad);
        const sy = clamp(curY + 18, my + pad + 18, my + mH - sh - pad);

        // Vector line showing cursor-below placement
        ctx.save();
        ctx.setLineDash([5, 5]);
        ctx.strokeStyle = 'rgba(96, 165, 250, 0.55)';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.moveTo(curX, curY + 12);
        ctx.lineTo(sx + sw * 0.5, sy);
        ctx.stroke();
        ctx.restore();

        ctx.save();
        ctx.globalAlpha = clamp(appear, 0, 1);
        drawLiquidGlassPanel(ctx, sx, sy, sw, sh, 14, null, W, H, { rim: 0.95, tint: 0.11 });
        ctx.font = `600 ${Math.round(W * 0.011)}px "Segoe UI", sans-serif`;
        ctx.fillStyle = '#93C5FD'; ctx.textAlign = 'center';
        ctx.fillText('鼠标下方落位 · 安全边距避让', sx + sw * 0.5, sy + sh * 0.52);
        ctx.restore();
      }

      // Draw crisp cursor arrow
      ctx.save();
      ctx.translate(curX, curY);
      ctx.beginPath();
      ctx.moveTo(0, 0); ctx.lineTo(0, 22); ctx.lineTo(6, 17); ctx.lineTo(14, 17); ctx.closePath();
      ctx.fillStyle = '#FFFFFF';
      ctx.shadowColor = 'rgba(56, 189, 248, 0.85)';
      ctx.shadowBlur = 12;
      ctx.fill();
      ctx.restore();

      const capAlpha = invLerp(0.15, 0.55, lt) * (1 - invLerp(2.6, 2.95, lt));
      drawCaption(ctx, W, H, '03 · CURSOR-AWARE MULTI-MONITOR', '目光所及，72% 指针随行', capAlpha, 'top');
    }

    // SHOT 4: Alt+V Liquid Glass Clipboard + Hotwords & Inline Calculator
    drawShot4LaunchpadEverything(ctx, W, H, lt) {
      ctx.drawImage(this.backdrop, 0, 0, W, H);
      const s = W / 1280;
      const split = easeInOutCubic(invLerp(1.35, 1.85, lt));
      const pw = lerp(W * 0.48, W * 0.56, split);
      const ph = lerp(H * 0.56, H * 0.49, split);
      const px = (W - pw) * 0.5;
      const py = H * 0.255;

      drawLiquidGlassPanel(ctx, px, py, pw, ph, 24 * s, this.backdrop, W, H, { rim: 1.0, tint: 0.065 });

      if (split < 0.5) {
        // Alt+V Liquid Glass Clipboard Overlay
        ctx.save();
        ctx.globalAlpha = 1 - split * 2;
        ctx.font = `600 ${Math.round(18 * s)}px "Segoe UI", "Microsoft YaHei UI", sans-serif`;
        ctx.textAlign = 'left'; ctx.fillStyle = '#93C5FD';
        ctx.fillText('Alt+V', px + 26 * s, py + 36 * s);
        ctx.fillStyle = '#F8FAFC';
        ctx.fillText('搜索剪贴板历史、来源应用...', px + 92 * s, py + 36 * s);
        ctx.fillStyle = 'rgba(255,255,255,0.12)';
        ctx.fillRect(px + 18 * s, py + 58 * s, pw - 36 * s, 1);

        const clips = [
          { badge: '■', badgeColor: '#3B82F6', title: '#3B82F6', sub: '★ 已置顶 · Figma · 实时色块预览' },
          { badge: 'PNG', badgeColor: '#10B981', title: '2054 × 1415 PNG 截图', sub: '自动 WIC 压缩缓存 · 告别 900MB 膨胀' },
          { badge: 'URL', badgeColor: '#0EA5E9', title: 'https://lightlaunchpad.thomasthzh.top', sub: 'Chrome · Shift+Enter 去格式纯文本粘贴' }
        ];
        clips.forEach((c, idx) => {
          const cy = py + 74 * s + idx * ((ph - 92 * s) / 3);
          const ch = (ph - 114 * s) / 3;
          rrect(ctx, px + 18 * s, cy, pw - 36 * s, ch, 12 * s);
          ctx.fillStyle = idx === 0 ? 'rgba(255,255,255,0.18)' : 'rgba(15,22,36,0.42)';
          ctx.fill();
          if (idx === 0) { ctx.strokeStyle = 'rgba(255,255,255,0.42)'; ctx.lineWidth = 1.2; ctx.stroke(); }

          rrect(ctx, px + 30 * s, cy + (ch - 34 * s) * 0.5, 42 * s, 34 * s, 8 * s);
          ctx.fillStyle = c.badgeColor; ctx.fill();
          if (c.badge !== '■') {
            ctx.font = `700 ${Math.round(11 * s)}px "Segoe UI", sans-serif`;
            ctx.fillStyle = '#fff'; ctx.textAlign = 'center';
            ctx.fillText(c.badge, px + 51 * s, cy + ch * 0.52);
          }
          ctx.textAlign = 'left';
          ctx.font = `600 ${Math.round(16 * s)}px "Microsoft YaHei UI", sans-serif`;
          ctx.fillStyle = '#F8FAFC';
          ctx.fillText(c.title, px + 84 * s, cy + ch * 0.36);
          ctx.font = `400 ${Math.round(12 * s)}px "Microsoft YaHei UI", sans-serif`;
          ctx.fillStyle = 'rgba(203,213,225,0.82)';
          ctx.fillText(c.sub, px + 84 * s, cy + ch * 0.68);
        });
        ctx.restore();
      } else {
        // Hotwords & Inline Calculator
        ctx.save();
        ctx.globalAlpha = (split - 0.5) * 2;
        ctx.font = `600 ${Math.round(24 * s)}px "Segoe UI", "Microsoft YaHei UI", sans-serif`;
        ctx.textAlign = 'left'; ctx.fillStyle = '#F8FAFC';
        ctx.fillText('128*36', px + 32 * s, py + 42 * s);
        ctx.fillStyle = 'rgba(255,255,255,0.12)';
        ctx.fillRect(px + 20 * s, py + 70 * s, pw - 40 * s, 1);

        const items = [
          { title: '= 4,608', sub: '128*36 · 按 Enter 立即复制结果到剪贴板', active: true },
          { title: '>recycle  清空回收站', sub: '[热词指令] · 零冲突系统动作，支持自定义 hotwords.json', active: false },
          { title: 'f 季度报告.pdf', sub: 'Everything 零依赖 IPC · 全盘文件 < 3ms 直达', active: false }
        ];
        items.forEach((f, idx) => {
          const fy = py + 86 * s + idx * ((ph - 104 * s) / 3);
          const fh = (ph - 122 * s) / 3;
          if (f.active) {
            rrect(ctx, px + 18 * s, fy, pw - 36 * s, fh, 12 * s);
            ctx.fillStyle = 'rgba(59, 130, 246, 0.28)';
            ctx.fill();
            ctx.strokeStyle = 'rgba(147, 197, 253, 0.5)'; ctx.stroke();
          }
          ctx.font = `700 ${Math.round(19 * s)}px "Microsoft YaHei UI", sans-serif`;
          ctx.fillStyle = '#F8FAFC';
          ctx.fillText(f.title, px + 34 * s, fy + fh * 0.38);
          ctx.font = `400 ${Math.round(13 * s)}px "Microsoft YaHei UI", sans-serif`;
          ctx.fillStyle = 'rgba(203,213,225,0.82)';
          ctx.fillText(f.sub, px + 34 * s, fy + fh * 0.72);
        });
        ctx.restore();
      }

      const capAlpha = invLerp(0.15, 0.55, lt) * (1 - invLerp(2.6, 2.95, lt));
      drawCaption(ctx, W, H, '04 · LIQUID CLIPBOARD & HOTWORDS', '液态剪贴板、自定义热词与即时计算', capAlpha, 'top');
    }

    // SHOT 5: Direct2D Input Stats & 2D ANSI Keyboard Heatmap
    drawShot5Heatmap(ctx, W, H, lt) {
      ctx.fillStyle = '#0c0f16';
      ctx.fillRect(0, 0, W, H);

      const cw = W * 0.72, ch = H * 0.52;
      const cx = (W - cw) * 0.5, cy = H * 0.27;
      rrect(ctx, cx, cy, cw, ch, 18);
      ctx.fillStyle = '#181c26';
      ctx.fill();
      ctx.strokeStyle = 'rgba(255,255,255,0.09)';
      ctx.lineWidth = 1.5;
      ctx.stroke();

      // KPI Strip
      const prog = easeOutExpo(invLerp(0.0, 1.5, lt));
      const kpis = [
        { k: '今日按键', v: Math.round(lerp(820, 1428, prog)).toLocaleString(), c: '#3B82F6' },
        { k: '鼠标点击', v: Math.round(lerp(140, 312, prog)).toLocaleString(), c: '#10B981' },
        { k: '滚轮滚动', v: Math.round(lerp(420, 864, prog)).toLocaleString(), c: '#F59E0B' },
        { k: '鼠标行程', v: `${lerp(42.0, 74.6, prog).toFixed(1)} m`, c: '#A855F7' }
      ];
      const kw = (cw - 60) / 4;
      kpis.forEach((m, i) => {
        const kx = cx + 18 + i * (kw + 8);
        rrect(ctx, kx, cy + 18, kw, 72, 12);
        ctx.fillStyle = '#202533';
        ctx.fill();
        rrect(ctx, kx + 14, cy + 28, 22, 3.5, 2);
        ctx.fillStyle = m.c; ctx.fill();
        ctx.font = `500 ${Math.round(W * 0.010)}px "Microsoft YaHei UI", sans-serif`;
        ctx.fillStyle = '#94A3B8'; ctx.textAlign = 'left';
        ctx.fillText(m.k, kx + 14, cy + 45);
        ctx.font = `700 ${Math.round(W * 0.018)}px "Segoe UI", sans-serif`;
        ctx.fillStyle = '#F8FAFC';
        ctx.fillText(m.v, kx + 14, cy + 72);
      });

      // Live 2D ANSI Keyboard Heatmap
      const rows = [
        ['`','1','2','3','4','5','6','7','8','9','0','-','=','Bksp'],
        ['Tab','Q','W','E','R','T','Y','U','I','O','P','[',']','\\'],
        ['Caps','A','S','D','F','G','H','J','K','L',';',"'",'Enter'],
        ['Shift','Z','X','C','V','B','N','M',',','.','/','Shift'],
        ['Ctrl','Win','Alt','Space','Alt','Ctrl','←','↑','↓','→']
      ];
      const hot = new Set(['E','A','S','D','Space','Enter','Bksp','I','O','N','T','R','Shift','Ctrl']);
      const kbTop = cy + 112, kbH = ch - 132, kbW = cw - 36;
      const rh = (kbH - 20) / 5;
      rows.forEach((row, r) => {
        const uw = (kbW - (row.length - 1) * 5) / row.length;
        row.forEach((k, c) => {
          const kx = cx + 18 + c * (uw + 5);
          const ky = kbTop + r * (rh + 5);
          const wave = Math.sin(lt * 2.5 + r * 0.7 + c * 0.4) * 0.5 + 0.5;
          const isHot = hot.has(k);
          const fill = k === 'Space' ? 'rgba(239, 68, 68, 0.92)'
            : isHot ? (wave > 0.5 ? 'rgba(245, 158, 11, 0.88)' : 'rgba(234, 88, 12, 0.88)')
            : ((r + c) % 3 === 0 ? 'rgba(14, 165, 233, 0.65)' : '#282e3d');
          rrect(ctx, kx, ky, uw, rh, 6);
          ctx.fillStyle = fill;
          ctx.fill();
          ctx.font = `600 ${Math.round(W * 0.0095)}px "Segoe UI", sans-serif`;
          ctx.fillStyle = '#F8FAFC'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
          ctx.fillText(k, kx + uw * 0.5, ky + rh * 0.5);
        });
      });

      const capAlpha = invLerp(0.15, 0.5, lt) * (1 - invLerp(2.2, 2.55, lt));
      drawCaption(ctx, W, H, '05 · LOCAL-ONLY PRIVACY TELEMETRY', '纯本地键鼠热力图 · 绝不记录明文', capAlpha, 'top');
    }

    // SHOT 6: Brand Convergence Finale
    drawShot6Finale(ctx, W, H, lt) {
      const bg = ctx.createRadialGradient(W * 0.5, H * 0.44, 10, W * 0.5, H * 0.44, W * 0.55);
      bg.addColorStop(0, '#131d36');
      bg.addColorStop(0.55, '#090d16');
      bg.addColorStop(1, '#04060a');
      ctx.fillStyle = bg;
      ctx.fillRect(0, 0, W, H);

      const enter = easeOutExpo(invLerp(0.0, 0.7, lt));
      const size = lerp(H * 0.22, H * 0.27, enter);
      drawEmblem2A(ctx, W * 0.5, H * 0.39, size, invLerp(0.35, 1.35, lt));

      ctx.save();
      ctx.globalAlpha = easeOutExpo(invLerp(0.25, 0.85, lt));
      ctx.textAlign = 'center';
      ctx.font = `700 ${Math.round(W * 0.044)}px "Segoe UI", "Microsoft YaHei UI", sans-serif`;
      ctx.fillStyle = '#FFFFFF';
      ctx.fillText('LightLaunchpad', W * 0.5, H * 0.64);

      ctx.font = `400 ${Math.round(W * 0.017)}px "Microsoft YaHei UI", "Segoe UI", sans-serif`;
      ctx.fillStyle = '#94A3B8';
      ctx.fillText('Windows 原生液态玻璃启动台 · 单文件 3.86 MB · V1.13', W * 0.5, H * 0.715);
      ctx.restore();
    }
  }

  window.addEventListener('DOMContentLoaded', () => {
    const canvas = document.getElementById('hero-film-canvas');
    if (!canvas) return;
    new HeroFilmPlayer(canvas, {
      playBtn: document.getElementById('film-play'),
      muteBtn: document.getElementById('film-mute'),
      replayBtn: document.getElementById('film-replay'),
      progressBar: document.getElementById('film-progress-fill'),
      timecode: document.getElementById('film-timecode'),
      chapters: Array.from(document.querySelectorAll('.film-chapter'))
    });
  });
})();
