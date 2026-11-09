  const nav = document.getElementById('navbar');
  const onScroll = () => nav.classList.toggle('scrolled', window.scrollY > 12);
  onScroll(); addEventListener('scroll', onScroll, { passive: true });

  const t = document.getElementById('navToggle'), mob = document.getElementById('navMobile');
  t.addEventListener('click', () => mob.classList.toggle('open'));
  mob.querySelectorAll('a').forEach(a => a.addEventListener('click', () => mob.classList.remove('open')));

  const io = new IntersectionObserver((es) => {
    es.forEach(e => { if (e.isIntersecting) { e.target.classList.add('in'); io.unobserve(e.target); } });
  }, { threshold: 0.12, rootMargin: '0px 0px -40px 0px' });
  document.querySelectorAll('.reveal').forEach((el, i) => {
    el.style.transitionDelay = Math.min(i % 4, 3) * 60 + 'ms';
    io.observe(el);
  });

  // /docs: botao Copy de cada bloco e destaque da secao atual
  document.querySelectorAll('[data-copy]').forEach(btn => {
    btn.addEventListener('click', async () => {
      const pre = btn.closest('.codewrap').querySelector('pre');
      try {
        await navigator.clipboard.writeText(pre.innerText);
        btn.textContent = 'Copied';
        setTimeout(() => btn.textContent = 'Copy', 1200);
      } catch (e) { btn.textContent = 'Ctrl+C'; }
    });
  });
  const links = [...document.querySelectorAll('.docnav [data-sec], .doctoc [data-sec]')];
  if (links.length && 'IntersectionObserver' in window) {
    const seen = new Map();
    const obs = new IntersectionObserver(es => {
      es.forEach(e => seen.set(e.target.id, e.isIntersecting));
      let atual = null;
      document.querySelectorAll('.docmain h2[id]').forEach(h => { if (!atual && seen.get(h.id)) atual = h.id; });
      links.forEach(a => a.classList.toggle('active', a.dataset.sec === atual));
    }, { rootMargin: '-88px 0px -70% 0px' });
    document.querySelectorAll('.docmain h2[id]').forEach(h => obs.observe(h));
  }
