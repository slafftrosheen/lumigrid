<script lang="ts">
  import { onMount } from 'svelte';
  import { NODE_LIST_KEY, getSelectedNodeIp, readString, readStringList } from '$lib/auth/storage';
  import { nodeStore } from '$lib/state/nodeStore';
  type Event={ts:number;online:boolean}; type DayData={date:string;onlineSecs:number;totalSecs:number;events:number};
  let nodeIp=$state(''); let nodeList=$state<string[]>([]); let selected=$state(''); let days=$state<DayData[]>([]); let stats=$state({uptimePct:0,reboots:0,longestStreak:0});
  onMount(()=>{
    nodeList=readStringList(NODE_LIST_KEY);
    const unsub = nodeStore.subscribe(val => {
      const ip = val.nodeIp || getSelectedNodeIp();
      if (ip && ip !== selected) {
        selected = ip;
        compute(ip);
      }
    });
    return unsub;
  });
  function compute(ip:string){selected=ip;const raw=readString(`lg_uptime_${ip}`);if(!raw){days=[];stats={uptimePct:0,reboots:0,longestStreak:0};return;}let events:Event[]=[];try{events=JSON.parse(raw)}catch{days=[];return;}const now=Date.now(),dayMap:Record<string,DayData>={};for(let i=0;i<91;i++){const d=new Date(now-i*86400000),key=d.toISOString().slice(0,10);dayMap[key]={date:key,onlineSecs:0,totalSecs:86400,events:0};}let reboots=0,prevOnline=true;for(let e=0;e<events.length;e++){const ev=events[e],next=events[e+1];if(!ev.online&&prevOnline)reboots++;prevOnline=ev.online;if(!ev.online)continue;let t=ev.ts;const end=next?next.ts:now;while(t<end){const d=new Date(t),key=d.toISOString().slice(0,10);if(dayMap[key]){const dayEnd=new Date(key+'T23:59:59.999Z').getTime();dayMap[key].onlineSecs+=(Math.min(end,dayEnd+1)-t)/1000;dayMap[key].events++;}t=new Date(d.toISOString().slice(0,10)+'T00:00:00Z').getTime()+86400000;}}const dayArr=Object.values(dayMap).sort((a,b)=>a.date.localeCompare(b.date));days=dayArr;const totalOn=dayArr.reduce((s,d)=>s+d.onlineSecs,0),total=dayArr.reduce((s,d)=>s+d.totalSecs,0);let maxStreak=0,cur=0;for(const d of dayArr){if(d.onlineSecs/86400>.5){cur++;maxStreak=Math.max(maxStreak,cur)}else cur=0}stats={uptimePct:total?totalOn/total*100:0,reboots,longestStreak:maxStreak};}
  const CELL=14,GAP=2,COLS=13,ROWS=7,W=COLS*(CELL+GAP),H=ROWS*(CELL+GAP); function cellColor(d:DayData){const pct=d.onlineSecs/86400;if(pct<.01)return'rgba(255,255,255,.05)';if(pct<.5)return'rgba(255,45,85,.25)';if(pct<.8)return'rgba(255,45,85,.55)';if(pct<.95)return'rgba(255,45,85,.80)';return'#ff2d55'} function cellTitle(d:DayData){return`${d.date}: ${((d.onlineSecs/86400)*100).toFixed(1)}% uptime (${d.events} events)`}
  let grid=$derived.by(()=>{if(!days.length)return[];const first=new Date(days[0].date+'T00:00:00Z'),startDow=(first.getDay()+6)%7,padded:(DayData|null)[]=Array(startDow).fill(null).concat(days);while(padded.length%7)padded.push(null);const weeks:(DayData|null)[][]=[];for(let i=0;i<padded.length;i+=7)weeks.push(padded.slice(i,i+7));return weeks.slice(-COLS)});const DOW_LABELS=['Mon','','Wed','','Fri','','Sun'];
</script>
<svelte:head><title>Uptime History — LumiGrid</title></svelte:head>
<div class="page"><header class="page-header"><div><h1>Uptime History</h1><p class="sub">Last 91 days · recorded locally per browser session</p></div></header>{#if nodeList.length>1}<div class="node-pills">{#each nodeList as ip}<button class="pill" class:active={selected===ip} onclick={()=>compute(ip)}>{ip}</button>{/each}</div>{/if}{#if !selected}<div class="empty">No nodes configured. Add a node in the sidebar.</div>{:else if !days.length}<div class="empty">No uptime data yet for <code>{selected}</code>. Visit the Fleet page to start recording.</div>{:else}<div class="stats-row"><div class="stat-card"><div class="stat-val" style:color={stats.uptimePct>95?'#34c759':stats.uptimePct>80?'#ff9500':'#ff453a'}>{stats.uptimePct.toFixed(1)}%</div><div class="stat-label">Avg Uptime</div></div><div class="stat-card"><div class="stat-val">{stats.reboots}</div><div class="stat-label">Reboots</div></div><div class="stat-card"><div class="stat-val">{stats.longestStreak}d</div><div class="stat-label">Best Streak</div></div></div><div class="heatmap-card"><div class="hm-wrap"><div class="dow-labels">{#each DOW_LABELS as l}<span class="dow-label">{l}</span>{/each}</div><svg viewBox={`0 0 ${W} ${H}`} class="heatmap-svg" aria-label="Uptime heatmap">{#each grid as week,wi}{#each week as cell,di}{#if cell}<rect x={wi*(CELL+GAP)} y={di*(CELL+GAP)} width={CELL} height={CELL} rx="3" fill={cellColor(cell)}><title>{cellTitle(cell)}</title></rect>{:else}<rect x={wi*(CELL+GAP)} y={di*(CELL+GAP)} width={CELL} height={CELL} rx="3" fill="rgba(255,255,255,.03)"/>{/if}{/each}{/each}</svg></div><div class="hm-legend"><span class="legend-label">Less</span>{#each [.05,.25,.55,.80,1] as op}<div class="legend-cell" style:background={op<.1?'rgba(255,255,255,.05)':`rgba(255,45,85,${op})`}></div>{/each}<span class="legend-label">More</span></div></div><div class="day-table-wrap"><table class="day-table"><thead><tr><th>Date</th><th>Uptime %</th><th>Online (h)</th><th>Events</th></tr></thead><tbody>{#each [...days].reverse().slice(0,30) as d}<tr><td class="mono">{d.date}</td><td><div class="bar-wrap"><div class="bar-fill" style:width={`${(d.onlineSecs/86400*100).toFixed(1)}%`} style:background={cellColor(d)}></div><span class="bar-val">{(d.onlineSecs/86400*100).toFixed(1)}%</span></div></td><td class="mono">{(d.onlineSecs/3600).toFixed(1)}</td><td class="mono">{d.events}</td></tr>{/each}</tbody></table></div>{/if}</div>
<style>
  .page { width:100%; max-width:980px; margin:0 auto; }
  .page-header {
    display:flex;
    align-items:flex-start;
    justify-content:space-between;
    margin-bottom:1.25rem;
    gap:1rem;
    padding:.85rem 1rem;
    background:rgba(22,22,24,.86);
    border:1px solid var(--dash-border);
    border-radius:var(--dash-radius-lg);
    box-shadow:var(--dash-shadow);
  }
  h1 { margin:0; font-size:1.6rem; font-weight:700; color:var(--dash-text); }
  .sub { margin:.25rem 0 0; color:var(--dash-muted); font-size:.85rem; }
  .empty {
    color:var(--dash-muted);
    padding:3rem 1rem;
    text-align:center;
    background:var(--dash-surface);
    border:1px dashed var(--dash-border);
    border-radius:var(--dash-radius-lg);
  }
  .empty code { background:rgba(255,255,255,.08); padding:.12em .4em; border-radius:6px; }
  .node-pills { display:flex; flex-wrap:wrap; gap:.5rem; margin-bottom:1.25rem; }
  .pill { background:rgba(255,255,255,.06); border:1px solid rgba(255,255,255,.12); border-radius:999px; color:var(--dash-muted); font-size:.78rem; font-family:monospace; padding:.35rem .75rem; cursor:pointer; }
  .pill.active {
    background:linear-gradient(90deg, rgba(var(--dash-accent-rgb),.2), rgba(var(--dash-accent-rgb),.1));
    border-color:rgba(var(--dash-accent-rgb),.36);
    color:#fff;
    box-shadow:inset 0 0 0 1px rgba(var(--dash-accent-rgb),.08);
  }
  .stats-row { display:grid; grid-template-columns:repeat(3,1fr); gap:1rem; margin-bottom:1.25rem; }
  .stat-card {
    background:linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
    border:1px solid var(--dash-border);
    border-radius:var(--dash-radius-lg);
    padding:1rem;
    text-align:center;
    box-shadow:var(--dash-shadow);
    transition:transform .18s ease,border-color .18s ease;
  }
  .stat-card:hover { transform:translateY(-1px); border-color:rgba(255,255,255,.13); }
  .stat-val { font-size:1.8rem; font-weight:700; color:var(--dash-text); line-height:1.1; }
  .stat-label { font-size:.72rem; color:var(--dash-muted); text-transform:uppercase; letter-spacing:.06em; margin-top:.25rem; }
  .heatmap-card {
    background:linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74));
    border:1px solid var(--dash-border);
    border-radius:var(--dash-radius-lg);
    padding:1.25rem;
    margin-bottom:1.25rem;
    overflow-x:auto;
    box-shadow:var(--dash-shadow);
  }
  .hm-wrap { display:flex; align-items:flex-start; gap:.5rem; min-width:max-content; }
  .dow-labels { display:flex; flex-direction:column; gap:2px; }
  .dow-label { font-size:.6rem; color:var(--dash-muted); height:14px; line-height:14px; width:24px; }
  .heatmap-svg { width:auto; height:auto; }
  .hm-legend { display:flex; align-items:center; gap:.35rem; margin-top:.75rem; }
  .legend-label { font-size:.65rem; color:var(--dash-muted); }
  .legend-cell { width:13px; height:13px; border-radius:3px; }
  .day-table-wrap { overflow-x:auto; background:linear-gradient(180deg, rgba(29,29,31,.74), rgba(18,18,20,.74)); border:1px solid var(--dash-border); border-radius:var(--dash-radius-lg); }
  .day-table { width:100%; border-collapse:collapse; font-size:.82rem; min-width:620px; }
  .day-table th { text-align:left; padding:.55rem .75rem; color:var(--dash-muted); font-size:.68rem; text-transform:uppercase; letter-spacing:.05em; border-bottom:1px solid rgba(255,255,255,.08); }
  .day-table td { padding:.5rem .75rem; color:var(--dash-text); border-bottom:1px solid rgba(255,255,255,.04); }
  .day-table tbody tr:hover { background:rgba(255,255,255,.025); }
  .mono { font-family:monospace; }
  .bar-wrap { display:flex; align-items:center; gap:.5rem; }
  .bar-fill { height:8px; border-radius:999px; min-width:2px; }
  .bar-val { font-size:.75rem; color:var(--dash-muted); font-family:monospace; min-width:4ch; }

  @media (max-width:680px) {
    .stats-row { grid-template-columns:1fr; }
    .stat-card { display:flex; align-items:center; justify-content:space-between; text-align:left; }
    .stat-label { margin-top:0; }
    .heatmap-card { padding:1rem; }
  }
</style>
