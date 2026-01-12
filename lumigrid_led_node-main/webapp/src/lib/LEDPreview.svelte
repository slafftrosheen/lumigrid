<script>
  import { onMount } from 'svelte';

  let canvas;
  let ctx;
  let pixels = Array(120).fill({ r: 0, g: 0, b: 0 });
  let animationId;

  const PIXEL_WIDTH = 8;
  const PIXEL_HEIGHT = 20;
  const PIXEL_GAP = 2;

  onMount(() => {
    ctx = canvas.getContext('2d');
    updateCanvas();
    
    // Start animation loop
    animate();
    
    return () => {
      if (animationId) {
        cancelAnimationFrame(animationId);
      }
    };
  });

  function updateCanvas() {
    if (!ctx) return;

    const width = canvas.width;
    const height = canvas.height;
    
    // Clear background
    ctx.fillStyle = '#1a1a1a';
    ctx.fillRect(0, 0, width, height);

    // Draw pixels
    pixels.forEach((pixel, i) => {
      const x = i * (PIXEL_WIDTH + PIXEL_GAP);
      const y = (height - PIXEL_HEIGHT) / 2;

      // Draw pixel glow
      const gradient = ctx.createRadialGradient(
        x + PIXEL_WIDTH / 2, y + PIXEL_HEIGHT / 2, 0,
        x + PIXEL_WIDTH / 2, y + PIXEL_HEIGHT / 2, PIXEL_WIDTH
      );
      
      const color = `rgb(${pixel.r}, ${pixel.g}, ${pixel.b})`;
      gradient.addColorStop(0, color);
      gradient.addColorStop(0.7, color);
      gradient.addColorStop(1, 'rgba(0, 0, 0, 0)');

      ctx.fillStyle = gradient;
      ctx.fillRect(x - 2, y - 2, PIXEL_WIDTH + 4, PIXEL_HEIGHT + 4);

      // Draw pixel
      ctx.fillStyle = color;
      ctx.fillRect(x, y, PIXEL_WIDTH, PIXEL_HEIGHT);
    });
  }

  function animate() {
    // Fetch LED data from device
    fetchLEDData();
    
    updateCanvas();
    animationId = requestAnimationFrame(animate);
  }

  async function fetchLEDData() {
    try {
      const response = await fetch('/api/leds/preview');
      if (response.ok) {
        const data = await response.json();
        if (data.pixels) {
          pixels = data.pixels;
        }
      }
    } catch (error) {
      // Fallback to demo animation if API not available
      updateDemoAnimation();
    }
  }

  function updateDemoAnimation() {
    const time = Date.now() / 1000;
    pixels = pixels.map((_, i) => {
      const hue = (i / pixels.length + time * 0.1) % 1.0;
      return hsvToRgb(hue, 1.0, 1.0);
    });
  }

  function hsvToRgb(h, s, v) {
    let r, g, b;
    const i = Math.floor(h * 6);
    const f = h * 6 - i;
    const p = v * (1 - s);
    const q = v * (1 - f * s);
    const t = v * (1 - (1 - f) * s);

    switch (i % 6) {
      case 0: r = v; g = t; b = p; break;
      case 1: r = q; g = v; b = p; break;
      case 2: r = p; g = v; b = t; break;
      case 3: r = p; g = q; b = v; break;
      case 4: r = t; g = p; b = v; break;
      case 5: r = v; g = p; b = q; break;
    }

    return {
      r: Math.round(r * 255),
      g: Math.round(g * 255),
      b: Math.round(b * 255)
    };
  }
</script>

<div class="w-full">
  <canvas
    bind:this={canvas}
    width={120 * (PIXEL_WIDTH + PIXEL_GAP)}
    height={100}
    class="w-full led-preview"
  />
</div>

<style>
  canvas {
    image-rendering: pixelated;
    image-rendering: crisp-edges;
  }
</style>
