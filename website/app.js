(() => {
  // 1. Interactive Liquid Glass Lab
  const preview = document.getElementById('glass-preview');
  const slBlur = document.getElementById('sl-blur');
  const slTint = document.getElementById('sl-tint');
  const slRim = document.getElementById('sl-rim');
  const outBlur = document.getElementById('out-blur');
  const outTint = document.getElementById('out-tint');
  const outRim = document.getElementById('out-rim');
  const badge = preview ? preview.querySelector('.gp-badge') : null;
  const presetBtns = Array.from(document.querySelectorAll('#lab-presets .seg-btn'));

  const PRESETS = {
    crystal: { label: 'Crystal', blur: 6, tint: 2, rim: 95 },
    frosted: { label: 'Frosted', blur: 48, tint: 12, rim: 75 },
    tinted: { label: 'Tinted', blur: 28, tint: 24, rim: 65 }
  };

  function applyGlass(presetName) {
    if (!preview) return;
    const blur = Number(slBlur.value);
    const tint = Number(slTint.value);
    const rim = Number(slRim.value);
    outBlur.textContent = `${blur}%`;
    outTint.textContent = `${tint}%`;
    outRim.textContent = `${rim}%`;

    const blurPx = (blur / 100) * 26;
    const tintAlpha = 0.04 + (tint / 40) * 0.48;
    const rimAlpha = Math.min(0.92, (rim / 150) * 0.85);
    preview.style.backdropFilter = `blur(${blurPx.toFixed(1)}px) saturate(145%)`;
    preview.style.background = `rgba(16, 22, 36, ${tintAlpha.toFixed(3)})`;
    preview.style.borderColor = `rgba(255, 255, 255, ${rimAlpha.toFixed(3)})`;
    preview.style.boxShadow = `0 24px 60px rgba(0,0,0,0.52), inset 0 1px 0 rgba(255,255,255,${(rimAlpha * 0.85).toFixed(3)})`;
    if (badge && presetName) badge.textContent = presetName;
  }

  presetBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      const p = PRESETS[btn.dataset.preset];
      if (!p) return;
      presetBtns.forEach(b => b.classList.toggle('is-active', b === btn));
      slBlur.value = p.blur;
      slTint.value = p.tint;
      slRim.value = p.rim;
      applyGlass(p.label);
    });
  });

  [slBlur, slTint, slRim].forEach(sl => {
    if (!sl) return;
    sl.addEventListener('input', () => {
      presetBtns.forEach(b => b.classList.remove('is-active'));
      applyGlass('Custom');
    });
  });
  applyGlass('Crystal');

  // 2. Dual-Screen 72% Cursor-Aware Placement Sandbox
  const sandbox = document.getElementById('dual-sandbox');
  const vspot = document.getElementById('vspot');
  const vcursor = document.getElementById('vcursor');
  if (sandbox && vspot && vcursor) {
    const updateSpot = (clientX, clientY) => {
      const rect = sandbox.getBoundingClientRect();
      const x = Math.max(0, Math.min(rect.width, clientX - rect.left));
      const y = Math.max(0, Math.min(rect.height, clientY - rect.top));
      vcursor.style.left = `${x}px`;
      vcursor.style.top = `${y}px`;

      const gap = 12;
      const monW = (rect.width - gap) * 0.5;
      const onRight = x >= monW + gap * 0.5;
      const monLeft = onRight ? monW + gap : 0;
      const monCx = monLeft + monW * 0.5;
      const monCy = rect.height * 0.5;

      const sw = 92, sh = 54, margin = 12;
      const targetCx = monCx + (x - monCx) * 0.72;
      const targetCy = monCy + (y - monCy) * 0.72;
      const sx = Math.max(monLeft + margin, Math.min(monLeft + monW - sw - margin, targetCx - sw * 0.5));
      const sy = Math.max(margin + 18, Math.min(rect.height - sh - margin, targetCy - sh * 0.5));
      vspot.style.left = `${sx}px`;
      vspot.style.top = `${sy}px`;
    };
    sandbox.addEventListener('pointermove', e => updateSpot(e.clientX, e.clientY));
    sandbox.addEventListener('pointerdown', e => updateSpot(e.clientX, e.clientY));
  }

  // 3. Settings Studio Screenshot Switcher
  const tabs = Array.from(document.querySelectorAll('.studio-tab'));
  const studioImg = document.getElementById('studio-img');
  const studioCap = document.getElementById('studio-caption');
  tabs.forEach(tab => {
    tab.addEventListener('click', () => {
      tabs.forEach(t => t.classList.toggle('is-active', t === tab));
      if (studioImg) studioImg.src = tab.dataset.shot;
      if (studioCap) studioCap.textContent = tab.dataset.caption;
    });
  });
})();
