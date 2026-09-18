<script lang="ts">
  import { tick } from 'svelte';

  type DialogTone = 'neutral' | 'warning' | 'danger';

  let {
    open = false,
    title,
    description = '',
    confirmLabel = 'Confirm',
    cancelLabel = 'Cancel',
    tone = 'neutral',
    inputLabel = '',
    inputPlaceholder = '',
    value = $bindable(''),
    busy = false,
    confirmDisabled = false,
    onconfirm,
    oncancel,
  } = $props<{
    open?: boolean;
    title: string;
    description?: string;
    confirmLabel?: string;
    cancelLabel?: string;
    tone?: DialogTone;
    inputLabel?: string;
    inputPlaceholder?: string;
    value?: string;
    busy?: boolean;
    confirmDisabled?: boolean;
    onconfirm?: (value: string) => void | Promise<void>;
    oncancel?: () => void;
  }>();

  let dialogEl: HTMLElement | null = null;

  $effect(() => {
    if (!open || typeof document === 'undefined') return;

    const previousFocus = document.activeElement as HTMLElement | null;
    const previousOverflow = document.body.style.overflow;
    document.body.style.overflow = 'hidden';

    tick().then(() => {
      const target = dialogEl?.querySelector<HTMLElement>(
        inputLabel ? 'input' : '[data-dialog-cancel]'
      );
      target?.focus();
    });

    return () => {
      document.body.style.overflow = previousOverflow;
      previousFocus?.focus?.();
    };
  });

  function cancel() {
    if (!busy) oncancel?.();
  }

  async function submit(event: SubmitEvent) {
    event.preventDefault();
    if (busy || confirmDisabled) return;
    await onconfirm?.(value);
  }

  function onKeydown(event: KeyboardEvent) {
    if (event.key === 'Escape') {
      event.preventDefault();
      cancel();
      return;
    }

    if (event.key !== 'Tab' || !dialogEl) return;

    const focusable = Array.from(
      dialogEl.querySelectorAll<HTMLElement>(
        'button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), a[href], [tabindex]:not([tabindex="-1"])'
      )
    ).filter(element => !element.hasAttribute('hidden'));

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
      if (event.target === event.currentTarget) cancel();
    }}
  >
    <form
      class="dialog-card"
      class:warning={tone === 'warning'}
      class:danger={tone === 'danger'}
      bind:this={dialogEl}
      role="dialog"
      aria-modal="true"
      aria-labelledby="operator-dialog-title"
      aria-describedby={description ? 'operator-dialog-description' : undefined}
      onsubmit={submit}
      onkeydown={onKeydown}
    >
      <div class="dialog-head">
        <div class="dialog-symbol" aria-hidden="true">
          {tone === 'danger' ? '!' : tone === 'warning' ? '△' : '•'}
        </div>
        <div class="dialog-copy">
          <h2 id="operator-dialog-title">{title}</h2>
          {#if description}
            <p id="operator-dialog-description">{description}</p>
          {/if}
        </div>
      </div>

      {#if inputLabel}
        <label class="dialog-field">
          <span>{inputLabel}</span>
          <input
            type="text"
            bind:value
            placeholder={inputPlaceholder}
            disabled={busy}
          />
        </label>
      {/if}

      <div class="dialog-actions">
        <button
          class="dialog-btn secondary"
          type="button"
          data-dialog-cancel
          onclick={cancel}
          disabled={busy}
        >
          {cancelLabel}
        </button>
        <button
          class="dialog-btn primary"
          class:warning={tone === 'warning'}
          class:danger={tone === 'danger'}
          type="submit"
          disabled={busy || confirmDisabled}
        >
          {busy ? 'Working…' : confirmLabel}
        </button>
      </div>
    </form>
  </div>
{/if}

<style>
  .dialog-backdrop {
    position: fixed;
    inset: 0;
    z-index: 2000;
    display: grid;
    place-items: center;
    padding: max(1rem, env(safe-area-inset-top)) 1rem max(1rem, env(safe-area-inset-bottom));
    background: rgba(0,0,0,.58);
    backdrop-filter: blur(10px);
    -webkit-backdrop-filter: blur(10px);
  }

  .dialog-card {
    width: min(460px, 100%);
    display: flex;
    flex-direction: column;
    gap: 1.2rem;
    padding: 1.15rem;
    background: linear-gradient(180deg, rgba(31,31,34,.985), rgba(18,18,20,.985));
    border: 1px solid rgba(255,255,255,.12);
    border-radius: var(--dash-radius-xl, 22px);
    box-shadow: 0 30px 90px rgba(0,0,0,.62);
    color: var(--dash-text, #f5f5f7);
  }

  .dialog-card.warning {
    border-color: color-mix(in oklab, var(--dash-warm, #ff9500) 34%, rgba(255,255,255,.12));
  }

  .dialog-card.danger {
    border-color: rgba(255,69,58,.36);
  }

  .dialog-head {
    display: grid;
    grid-template-columns: 38px minmax(0, 1fr);
    gap: .8rem;
    align-items: start;
  }

  .dialog-symbol {
    width: 38px;
    height: 38px;
    display: grid;
    place-items: center;
    border-radius: 12px;
    background: rgba(var(--dash-accent-rgb, 255,45,85),.13);
    border: 1px solid rgba(var(--dash-accent-rgb, 255,45,85),.22);
    color: var(--dash-accent, #ff2d55);
    font-size: 1rem;
    font-weight: 900;
  }

  .warning .dialog-symbol {
    background: color-mix(in oklab, var(--dash-warm, #ff9500) 13%, transparent);
    border-color: color-mix(in oklab, var(--dash-warm, #ff9500) 28%, transparent);
    color: var(--dash-warm, #ff9500);
  }

  .danger .dialog-symbol {
    background: rgba(255,69,58,.13);
    border-color: rgba(255,69,58,.28);
    color: #ff453a;
  }

  .dialog-copy {
    min-width: 0;
  }

  .dialog-copy h2 {
    margin: .1rem 0 0;
    font-size: 1.05rem;
    line-height: 1.25;
    color: #fff;
  }

  .dialog-copy p {
    margin: .4rem 0 0;
    color: var(--dash-muted, #a1a1a6);
    font-size: .86rem;
    line-height: 1.5;
  }

  .dialog-field {
    display: flex;
    flex-direction: column;
    gap: .45rem;
    color: #d1d1d6;
    font-size: .78rem;
    font-weight: 700;
  }

  .dialog-field input {
    width: 100%;
    min-height: 42px;
    padding: .65rem .75rem;
    border: 1px solid rgba(255,255,255,.13);
    border-radius: 10px;
    background: rgba(255,255,255,.055);
    color: #fff;
    font: inherit;
  }

  .dialog-field input:focus-visible {
    border-color: var(--dash-accent, #ff2d55);
    outline: 2px solid var(--dash-accent, #ff2d55);
    outline-offset: 2px;
  }

  .dialog-actions {
    display: flex;
    justify-content: flex-end;
    gap: .55rem;
  }

  .dialog-btn {
    min-height: 38px;
    padding: .55rem .9rem;
    border-radius: 10px;
    font-size: .82rem;
    font-weight: 750;
    cursor: pointer;
  }

  .dialog-btn.secondary {
    background: rgba(255,255,255,.045);
    border: 1px solid rgba(255,255,255,.11);
    color: #d1d1d6;
  }

  .dialog-btn.primary {
    background: var(--dash-accent, #ff2d55);
    border: 1px solid transparent;
    color: #fff;
    box-shadow: 0 8px 22px rgba(var(--dash-accent-rgb, 255,45,85),.22);
  }

  .dialog-btn.primary.warning {
    background: var(--dash-warm, #ff9500);
    color: #161616;
    box-shadow: 0 8px 22px rgba(255,149,0,.18);
  }

  .dialog-btn.primary.danger {
    background: #ff453a;
    color: #fff;
    box-shadow: 0 8px 22px rgba(255,69,58,.2);
  }

  .dialog-btn:disabled {
    opacity: .45;
    cursor: not-allowed;
  }

  @media (max-width: 520px) {
    .dialog-backdrop {
      place-items: end center;
      padding-inline: .65rem;
      padding-bottom: calc(.65rem + env(safe-area-inset-bottom));
    }

    .dialog-card {
      border-radius: 22px;
    }

    .dialog-actions {
      flex-direction: column-reverse;
    }

    .dialog-btn {
      width: 100%;
      min-height: 44px;
    }
  }
</style>
