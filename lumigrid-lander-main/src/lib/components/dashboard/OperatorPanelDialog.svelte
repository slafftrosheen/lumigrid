<script lang="ts">
  import { tick, type Snippet } from 'svelte';

  let {
    open = false,
    title,
    description = '',
    maxWidth = '780px',
    allowBackdropClose = true,
    onclose,
    children,
    footer,
  } = $props<{
    open?: boolean;
    title: string;
    description?: string;
    maxWidth?: string;
    allowBackdropClose?: boolean;
    onclose?: () => void;
    children?: Snippet;
    footer?: Snippet;
  }>();

  let dialogEl = $state<HTMLElement | null>(null);

  $effect(() => {
    if (!open || typeof document === 'undefined') return;

    const previousFocus = document.activeElement as HTMLElement | null;
    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = 'hidden';

    tick().then(() => {
      const target = dialogEl?.querySelector<HTMLElement>(
        '[data-panel-close], button:not([disabled]), input:not([disabled])'
      );
      target?.focus();
    });

    return () => {
      document.body.style.overflow = previousOverflow;
      previousFocus?.focus?.();
    };
  });

  function close() {
    onclose?.();
  }

  function onKeydown(event: KeyboardEvent) {
    if (event.key === 'Escape') {
      event.preventDefault();
      close();
      return;
    }

    if (event.key !== 'Tab' || !dialogEl) return;

    const focusable = Array.from(
      dialogEl.querySelectorAll<HTMLElement>(
        'button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), a[href], [tabindex]:not([tabindex="-1"])'
      )
    ).filter((element) => !element.hasAttribute('hidden'));

    if (!focusable.length) return;

    const first = focusable[0];
    const last = focusable[focusable.length - 1];
    const active = document.activeElement;

    if (event.shiftKey && (active === first || !dialogEl.contains(active))) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && active === last) {
      event.preventDefault();
      first.focus();
    }
  }
</script>

{#if open}
  <div
    class="dialog-backdrop"
    role="presentation"
    onclick={(event) => {
      if (allowBackdropClose && event.target === event.currentTarget) close();
    }}
  >
    <!-- svelte-ignore a11y_no_noninteractive_element_to_interactive_role -->
    <!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
    <div
      class="panel-card"
      style:max-width={maxWidth}
      bind:this={dialogEl}
      role="dialog"
      tabindex="-1"
      aria-modal="true"
      aria-labelledby="operator-panel-dialog-title"
      aria-describedby={description ? 'operator-panel-dialog-description' : undefined}
      onkeydown={onKeydown}
    >
      <header class="panel-header">
        <div class="panel-copy">
          <h2 id="operator-panel-dialog-title">{title}</h2>
          {#if description}
            <p id="operator-panel-dialog-description">{description}</p>
          {/if}
        </div>
        <button
          class="panel-close-btn"
          type="button"
          data-panel-close
          onclick={close}
          aria-label="Close dialog"
        >
          <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
            <line x1="18" y1="6" x2="6" y2="18"></line>
            <line x1="6" y1="6" x2="18" y2="18"></line>
          </svg>
        </button>
      </header>

      <div class="panel-content">
        {#if children}
          {@render children()}
        {/if}
      </div>

      {#if footer}
        <footer class="panel-footer">
          {@render footer()}
        </footer>
      {/if}
    </div>
  </div>
{/if}

<style>
  .dialog-backdrop {
    position: fixed;
    inset: 0;
    z-index: 2100;
    display: grid;
    place-items: center;
    padding: max(1.2rem, env(safe-area-inset-top)) 1rem max(1.2rem, env(safe-area-inset-bottom));
    background: rgba(0, 0, 0, 0.65);
    backdrop-filter: blur(12px);
    -webkit-backdrop-filter: blur(12px);
  }

  .panel-card {
    width: 100%;
    max-height: calc(100vh - 3rem);
    display: flex;
    flex-direction: column;
    background: linear-gradient(180deg, var(--dash-surface-strong, #1c1c1f), var(--dash-surface, #141416));
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.12));
    border-radius: var(--dash-radius-xl, 22px);
    box-shadow: 0 35px 95px rgba(0, 0, 0, 0.7);
    color: var(--dash-text, #f5f5f7);
    overflow: hidden;
  }

  .panel-header {
    display: flex;
    align-items: flex-start;
    justify-content: space-between;
    gap: 1rem;
    padding: 1.25rem 1.4rem;
    border-bottom: 1px solid var(--dash-border, rgba(255, 255, 255, 0.08));
    background: rgba(255, 255, 255, 0.02);
  }

  .panel-copy h2 {
    margin: 0;
    font-size: 1.15rem;
    font-weight: 750;
    letter-spacing: -0.01em;
    color: #fff;
  }

  .panel-copy p {
    margin: 0.35rem 0 0;
    font-size: 0.86rem;
    color: var(--dash-muted, #a1a1a6);
    line-height: 1.45;
  }

  .panel-close-btn {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 34px;
    height: 34px;
    border-radius: 9px;
    background: rgba(255, 255, 255, 0.06);
    border: 1px solid var(--dash-border, rgba(255, 255, 255, 0.1));
    color: var(--dash-muted, #a1a1a6);
    cursor: pointer;
    transition: all 0.15s ease;
    flex-shrink: 0;
  }

  .panel-close-btn:hover {
    background: rgba(255, 255, 255, 0.12);
    color: #fff;
  }

  .panel-content {
    padding: 1.25rem 1.4rem;
    overflow-y: auto;
    flex: 1 1 auto;
  }

  .panel-footer {
    display: flex;
    justify-content: flex-end;
    align-items: center;
    gap: 0.75rem;
    padding: 1rem 1.4rem;
    border-top: 1px solid var(--dash-border, rgba(255, 255, 255, 0.08));
    background: rgba(0, 0, 0, 0.2);
  }

  @media (max-width: 640px) {
    .dialog-backdrop {
      place-items: end center;
      padding-inline: 0;
      padding-bottom: 0;
      padding-top: max(1rem, env(safe-area-inset-top));
    }

    .panel-card {
      max-height: 90vh;
      border-bottom-left-radius: 0;
      border-bottom-right-radius: 0;
      border-top-left-radius: 22px;
      border-top-right-radius: 22px;
    }

    .panel-footer {
      flex-direction: column-reverse;
      padding-bottom: calc(1rem + env(safe-area-inset-bottom));
    }

    .panel-footer :global(button) {
      width: 100%;
      min-height: 44px;
    }
  }
</style>
