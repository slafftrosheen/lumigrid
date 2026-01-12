<script>
  import { base } from '$app/paths';
  export let title = 'Window display upgrade';
  export let client = 'Local Boutique';
  export let outcome = [
    { k: 'Install time', v: '↓ 35%' },
    { k: 'Power use', v: '↓ 22%' },
    { k: 'Passerby stop rate', v: '↑ 41%' }
  ];
  export let img = 'https://images.unsplash.com/photo-1515879218367-8466d910aaa4';
</script>

<section class="section container card case-study" aria-labelledby="cs-h">
  <h2 id="cs-h">Case study</h2>
  <div class="grid grid-2 content">
    <img src={img} alt="" loading="lazy" />
    <div>
      <h3>{title}</h3>
      <p class="client">Client: {client}</p>
      <ul class="outcomes">
        {#each outcome as o}
          <li class="card outcome-item">
            <span>{o.k}</span><strong>{o.v}</strong>
          </li>
        {/each}
      </ul>
      <a class="btn" href={`${base}/contact#contact`}>Ask about this setup</a>
    </div>
  </div>
</section>

<style>
  .case-study h2 {
    font-size: var(--fs-h2);
    margin: 0 0 var(--spacing-mobile-sm);
  }
  .content {
    align-items: center;
  }
  .content img {
    width: 100%;
    height: auto;
    border-radius: var(--radius-md);
    object-fit: cover;
  }
  .content h3 {
    margin: 0.25rem 0 0;
  }
  .client {
    color: var(--muted);
    margin: 0.25rem 0;
  }
  .outcomes {
    list-style: none;
    padding: 0;
    margin: var(--spacing-mobile-sm) 0;
    display: grid;
    gap: var(--spacing-mobile-xs);
  }
  .outcome-item {
    padding: var(--spacing-mobile-xs) var(--spacing-mobile-sm);
    display: flex;
    justify-content: space-between;
  }
</style>
