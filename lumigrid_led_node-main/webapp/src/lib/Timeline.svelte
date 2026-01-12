<script>
  import { onMount, onDestroy } from 'svelte';

  export let sequence;
  export let playhead = 0;
  export let playing = false;

  let canvas;
  let ctx;
  let zoom = 1.0;
  let scrollX = 0;
  let isDragging = false;
  let dragStartX = 0;

  const PIXELS_PER_SECOND = 100;
  const TRACK_HEIGHT = 40;
  const TIMELINE_HEIGHT = 30;

  onMount(() => {
    ctx = canvas.getContext('2d');
    resizeCanvas();
    drawTimeline();

    window.addEventListener('resize', resizeCanvas);
    
    return () => {
      window.removeEventListener('resize', resizeCanvas);
    };
  });

  $: if (canvas && sequence) {
    drawTimeline();
  }

  $: if (canvas && playhead !== undefined) {
    drawTimeline();
  }

  function resizeCanvas() {
    if (!canvas) return;
    const parent = canvas.parentElement;
    canvas.width = parent.clientWidth;
    canvas.height = Math.max(400, (sequence?.tracks?.length || 1) * TRACK_HEIGHT + TIMELINE_HEIGHT + 20);
  }

  function drawTimeline() {
    if (!ctx || !canvas) return;

    const width = canvas.width;
    const height = canvas.height;

    // Clear
    ctx.fillStyle = '#1a1a1a';
    ctx.fillRect(0, 0, width, height);

    // Draw timeline ruler
    drawTimeRuler();

    // Draw tracks
    if (sequence && sequence.tracks) {
      sequence.tracks.forEach((track, index) => {
        drawTrack(track, index);
      });
    }

    // Draw events
    if (sequence && sequence.events) {
      sequence.events.forEach(event => {
        drawEvent(event);
      });
    }

    // Draw playhead
    drawPlayhead();

    // Draw grid
    drawGrid();
  }

  function drawTimeRuler() {
    ctx.fillStyle = '#2a2a2a';
    ctx.fillRect(0, 0, canvas.width, TIMELINE_HEIGHT);

    ctx.strokeStyle = '#555';
    ctx.fillStyle = '#aaa';
    ctx.font = '11px monospace';

    const secondsVisible = canvas.width / (PIXELS_PER_SECOND * zoom);
    const interval = secondsVisible > 60 ? 10 : secondsVisible > 30 ? 5 : 1;

    for (let sec = 0; sec < sequence?.total_duration_ms / 1000 || 60; sec += interval) {
      const x = sec * PIXELS_PER_SECOND * zoom - scrollX;
      
      if (x >= 0 && x <= canvas.width) {
        ctx.beginPath();
        ctx.moveTo(x, TIMELINE_HEIGHT - 5);
        ctx.lineTo(x, TIMELINE_HEIGHT);
        ctx.stroke();

        const minutes = Math.floor(sec / 60);
        const seconds = sec % 60;
        const label = `${minutes}:${seconds.toString().padStart(2, '0')}`;
        ctx.fillText(label, x + 2, TIMELINE_HEIGHT - 8);
      }
    }
  }

  function drawTrack(track, index) {
    const y = TIMELINE_HEIGHT + index * TRACK_HEIGHT;

    // Track background
    ctx.fillStyle = track.enabled ? '#2a2a2a' : '#1a1a1a';
    ctx.fillRect(0, y, canvas.width, TRACK_HEIGHT);

    // Track border
    ctx.strokeStyle = '#333';
    ctx.strokeRect(0, y, canvas.width, TRACK_HEIGHT);

    // Track label
    ctx.fillStyle = track.enabled ? '#fff' : '#666';
    ctx.font = 'bold 12px sans-serif';
    ctx.fillText(track.name, 8, y + 20);

    // Channel indicator
    ctx.fillStyle = track.channel < 8 ? '#4a9eff' : '#ff9a4a';
    ctx.fillRect(8, y + TRACK_HEIGHT - 8, 30, 4);
  }

  function drawEvent(event) {
    if (!sequence || !sequence.tracks) return;

    const track = sequence.tracks[event.track_id];
    if (!track) return;

    const x = (event.timestamp_ms / 1000) * PIXELS_PER_SECOND * zoom - scrollX;
    const y = TIMELINE_HEIGHT + event.track_id * TRACK_HEIGHT + 4;
    const width = (event.duration_ms / 1000) * PIXELS_PER_SECOND * zoom;
    const height = TRACK_HEIGHT - 8;

    // Event colors by type
    const colors = {
      0: '#4a9eff', // EFFECT_START
      1: '#ff4a4a', // EFFECT_STOP
      2: '#4aff9a', // KEYFRAME
      3: '#ff9a4a', // CUE
      4: '#9a4aff', // TEMPO_CHANGE
      5: '#ffff4a'  // SYNC_MARKER
    };

    ctx.fillStyle = colors[event.event_type] || '#666';
    ctx.fillRect(x, y, Math.max(width, 2), height);

    // Event border
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = 1;
    ctx.strokeRect(x, y, Math.max(width, 2), height);
  }

  function drawGrid() {
    if (!sequence) return;

    const bpm = sequence.bpm || 120;
    const beatInterval = (60 / bpm) * PIXELS_PER_SECOND * zoom;

    ctx.strokeStyle = '#333';
    ctx.lineWidth = 1;
    ctx.setLineDash([2, 4]);

    for (let x = -scrollX % beatInterval; x < canvas.width; x += beatInterval) {
      ctx.beginPath();
      ctx.moveTo(x, TIMELINE_HEIGHT);
      ctx.lineTo(x, canvas.height);
      ctx.stroke();
    }

    ctx.setLineDash([]);
  }

  function drawPlayhead() {
    const x = (playhead / 1000) * PIXELS_PER_SECOND * zoom - scrollX;

    if (x >= 0 && x <= canvas.width) {
      // Playhead line
      ctx.strokeStyle = '#ff0000';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, canvas.height);
      ctx.stroke();

      // Playhead indicator
      ctx.fillStyle = '#ff0000';
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x - 6, 10);
      ctx.lineTo(x + 6, 10);
      ctx.closePath();
      ctx.fill();
    }
  }

  function handleMouseDown(e) {
    isDragging = true;
    dragStartX = e.clientX + scrollX;
  }

  function handleMouseMove(e) {
    if (isDragging) {
      scrollX = dragStartX - e.clientX;
      scrollX = Math.max(0, scrollX);
      drawTimeline();
    }
  }

  function handleMouseUp() {
    isDragging = false;
  }

  function handleWheel(e) {
    e.preventDefault();
    if (e.ctrlKey) {
      // Zoom
      const delta = e.deltaY > 0 ? 0.9 : 1.1;
      zoom = Math.max(0.1, Math.min(4.0, zoom * delta));
    } else {
      // Scroll
      scrollX += e.deltaY;
      scrollX = Math.max(0, scrollX);
    }
    drawTimeline();
  }
</script>

<div class="relative">
  <canvas
    bind:this={canvas}
    class="timeline-canvas w-full"
    on:mousedown={handleMouseDown}
    on:mousemove={handleMouseMove}
    on:mouseup={handleMouseUp}
    on:mouseleave={handleMouseUp}
    on:wheel={handleWheel}
  />
  
  <div class="absolute top-2 right-2 flex gap-2">
    <button class="btn btn-xs" on:click={() => { zoom *= 1.2; drawTimeline(); }}>
      🔍+
    </button>
    <button class="btn btn-xs" on:click={() => { zoom *= 0.8; drawTimeline(); }}>
      🔍-
    </button>
  </div>
</div>
