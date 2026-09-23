<script lang="ts">
  import {
    sampleAutomationTrack,
    applyCurveQ15,
    curveIdToName,
    type TransitionCurve,
    type AutomationTrack,
    type AutomationPoint,
  } from '$lib/runtime/curves';

  let {
    track = $bindable<AutomationTrack>({
      target: 0,
      curve: 3, // easeInOut
      periodMs: 5000,
      loop: true,
      points: [
        { position: 0, value: 0 },
        { position: 32768, value: 65535 },
        { position: 65535, value: 0 },
      ],
    }),
    elapsedMs = 0,
    readOnly = false,
    color = '#ff2d55',
    onChange = (_points: AutomationPoint[]) => {},
  }: {
    track?: AutomationTrack;
    elapsedMs?: number;
    readOnly?: boolean;
    color?: string;
    onChange?: (points: AutomationPoint[]) => void;
  } = $props();

  let svgElement: SVGSVGElement | null = $state(null);
  let draggingIdx = $state<number | null>(null);

  const WIDTH = 400;
  const HEIGHT = 140;
  const PADDING_X = 20;
  const PADDING_Y = 15;
  const PLOT_W = WIDTH - 2 * PADDING_X;
  const PLOT_H = HEIGHT - 2 * PADDING_Y;

  function normToSvg(posNorm: number, valNorm: number): { x: number; y: number } {
    const x = PADDING_X + (posNorm / 65535) * PLOT_W;
    const y = PADDING_Y + (1 - valNorm / 65535) * PLOT_H;
    return { x, y };
  }

  function svgToNorm(x: number, y: number): { pos: number; val: number } {
    const clampedX = Math.max(PADDING_X, Math.min(PADDING_X + PLOT_W, x));
    const clampedY = Math.max(PADDING_Y, Math.min(PADDING_Y + PLOT_H, y));
    const pos = Math.round(((clampedX - PADDING_X) / PLOT_W) * 65535);
    const val = Math.round((1 - (clampedY - PADDING_Y) / PLOT_H) * 65535);
    return { pos, val };
  }

  const curvePath = $derived.by(() => {
    if (!track.points || track.points.length === 0) return '';
    const numSamples = 80;
    const pointsStr: string[] = [];

    for (let i = 0; i <= numSamples; ++i) {
      const tNorm = i / numSamples;
      const fakeElapsed = tNorm * (track.periodMs || 5000);
      const val = sampleAutomationTrack(track, fakeElapsed);
      const pt = normToSvg(tNorm * 65535, val);
      pointsStr.push(`${i === 0 ? 'M' : 'L'} ${pt.x.toFixed(1)} ${pt.y.toFixed(1)}`);
    }
    return pointsStr.join(' ');
  });

  const areaPath = $derived.by(() => {
    if (!curvePath) return '';
    const startPt = normToSvg(0, 0);
    const endPt = normToSvg(65535, 0);
    return `${curvePath} L ${endPt.x} ${endPt.y} L ${startPt.x} ${startPt.y} Z`;
  });

  const playheadX = $derived.by(() => {
    const period = track.periodMs || 5000;
    const posNorm = track.loop ? (elapsedMs % period) / period : Math.min(1, elapsedMs / period);
    return PADDING_X + posNorm * PLOT_W;
  });

  function onPointerDown(idx: number, e: PointerEvent) {
    if (readOnly) return;
    e.stopPropagation();
    draggingIdx = idx;
    (e.target as HTMLElement).setPointerCapture(e.pointerId);
  }

  function onPointerMove(e: PointerEvent) {
    if (draggingIdx === null || !svgElement || readOnly) return;
    const rect = svgElement.getBoundingClientRect();
    const scaleX = WIDTH / rect.width;
    const scaleY = HEIGHT / rect.height;
    const svgX = (e.clientX - rect.left) * scaleX;
    const svgY = (e.clientY - rect.top) * scaleY;

    const { pos, val } = svgToNorm(svgX, svgY);

    const newPoints = [...track.points];
    newPoints[draggingIdx] = {
      position: pos,
      value: val,
    };
    // Keep sorted by position
    newPoints.sort((a, b) => a.position - b.position);
    track.points = newPoints;
    onChange(newPoints);
    draggingIdx = newPoints.findIndex(p => p.position === pos && p.value === val);
  }

  function onPointerUp(e: PointerEvent) {
    if (draggingIdx !== null) {
      try {
        (e.target as HTMLElement).releasePointerCapture(e.pointerId);
      } catch {}
      draggingIdx = null;
    }
  }

  function onSvgClick(e: MouseEvent) {
    if (readOnly || draggingIdx !== null || !svgElement) return;
    if (track.points.length >= 6) return;

    const rect = svgElement.getBoundingClientRect();
    const scaleX = WIDTH / rect.width;
    const scaleY = HEIGHT / rect.height;
    const svgX = (e.clientX - rect.left) * scaleX;
    const svgY = (e.clientY - rect.top) * scaleY;

    const { pos, val } = svgToNorm(svgX, svgY);
    const newPoints = [...track.points, { position: pos, value: val }];
    newPoints.sort((a, b) => a.position - b.position);
    track.points = newPoints;
    onChange(newPoints);
  }

  function removePoint(idx: number, e: MouseEvent) {
    e.stopPropagation();
    if (readOnly || track.points.length <= 2) return;
    const newPoints = track.points.filter((_, i) => i !== idx);
    track.points = newPoints;
    onChange(newPoints);
  }
</script>

<div class="curve-container">
  <svg
    bind:this={svgElement}
    viewBox="0 0 {WIDTH} {HEIGHT}"
    class="curve-svg"
    onpointermove={onPointerMove}
    onpointerup={onPointerUp}
    onclick={onSvgClick}
    role="presentation"
  >
    <defs>
      <linearGradient id="curveFill-{color.replace('#','')}" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stop-color={color} stop-opacity="0.25" />
        <stop offset="100%" stop-color={color} stop-opacity="0.02" />
      </linearGradient>
    </defs>

    <!-- Grid lines -->
    {#each [0.25, 0.5, 0.75] as gridY}
      <line
        x1={PADDING_X}
        y1={PADDING_Y + gridY * PLOT_H}
        x2={PADDING_X + PLOT_W}
        y2={PADDING_Y + gridY * PLOT_H}
        stroke="rgba(255, 255, 255, 0.07)"
        stroke-dasharray="3,3"
      />
    {/each}
    {#each [0.25, 0.5, 0.75] as gridX}
      <line
        x1={PADDING_X + gridX * PLOT_W}
        y1={PADDING_Y}
        x2={PADDING_X + gridX * PLOT_W}
        y2={PADDING_Y + PLOT_H}
        stroke="rgba(255, 255, 255, 0.07)"
        stroke-dasharray="3,3"
      />
    {/each}

    <!-- Zero and 100% boundary lines -->
    <rect
      x={PADDING_X}
      y={PADDING_Y}
      width={PLOT_W}
      height={PLOT_H}
      fill="none"
      stroke="rgba(255, 255, 255, 0.15)"
      stroke-width="1"
    />

    <!-- Filled Area under Curve -->
    {#if areaPath}
      <path d={areaPath} fill="url(#curveFill-{color.replace('#','')})" />
    {/if}

    <!-- Curve Line -->
    {#if curvePath}
      <path
        d={curvePath}
        fill="none"
        stroke={color}
        stroke-width="2.5"
        stroke-linecap="round"
        stroke-linejoin="round"
      />
    {/if}

    <!-- Live Playhead Line -->
    {#if elapsedMs > 0}
      <line
        x1={playheadX}
        y1={PADDING_Y}
        x2={playheadX}
        y2={PADDING_Y + PLOT_H}
        stroke="#ffffff"
        stroke-width="1.5"
        opacity="0.85"
      />
    {/if}

    <!-- Control Points -->
    {#each track.points as pt, idx}
      {@const coords = normToSvg(pt.position, pt.value)}
      <!-- svelte-ignore a11y_no_static_element_interactions -->
      <g
        class="point-handle"
        class:active={draggingIdx === idx}
        onpointerdown={(e) => onPointerDown(idx, e)}
        oncontextmenu={(e) => { e.preventDefault(); removePoint(idx, e); }}
      >
        <circle
          cx={coords.x}
          cy={coords.y}
          r="9"
          fill="rgba(0, 0, 0, 0.7)"
          stroke={color}
          stroke-width="2"
        />
        <circle
          cx={coords.x}
          cy={coords.y}
          r="4"
          fill={color}
        />
        <title>Point {idx + 1}: {Math.round((pt.position / 65535) * 100)}% time, {Math.round((pt.value / 65535) * 100)}% val</title>
      </g>
    {/each}
  </svg>

  <div class="curve-hints">
    <span>0%</span>
    <span class="hint-text">{readOnly ? 'Curve Preview' : 'Click to add point • Drag to move • Right-click to remove (max 6)'}</span>
    <span>100%</span>
  </div>
</div>

<style>
  .curve-container {
    display: flex;
    flex-direction: column;
    width: 100%;
    background: rgba(0,0,0,.18);
    border: 1px solid rgba(255,255,255,.08);
    border-radius: 12px;
    padding: 10px;
    box-sizing: border-box;
  }

  .curve-svg {
    width: 100%;
    height: 140px;
    display: block;
    cursor: crosshair;
    user-select: none;
    touch-action: none;
  }

  .point-handle {
    cursor: grab;
    transition: transform 0.08s ease;
  }

  .point-handle:hover circle:first-child {
    stroke-width: 3;
    filter: drop-shadow(0 0 6px rgba(255, 45, 85, 0.8));
  }

  .point-handle.active {
    cursor: grabbing;
  }

  .curve-hints {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-family: monospace;
    font-size: 0.7rem;
    color: rgba(255, 255, 255, 0.4);
    margin-top: 6px;
    padding: 0 4px;
  }

  .hint-text {
    font-style: italic;
    color: rgba(255, 255, 255, 0.3);
  }
</style>
