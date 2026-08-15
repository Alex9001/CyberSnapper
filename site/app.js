const repository = 'https://github.com/Alex9001/CyberSnapper';
const api = 'https://api.github.com/repos/Alex9001/CyberSnapper/releases';

const navToggle = document.querySelector('.nav-toggle');
const navLinks = document.querySelector('.nav-links');
navToggle?.addEventListener('click', () => {
  const open = navToggle.getAttribute('aria-expanded') !== 'true';
  navToggle.setAttribute('aria-expanded', String(open));
  navLinks?.classList.toggle('is-open', open);
});
navLinks?.addEventListener('click', (event) => {
  if (event.target.closest('a')) {
    navToggle?.setAttribute('aria-expanded', 'false');
    navLinks.classList.remove('is-open');
  }
});

const lightbox = document.querySelector('.lightbox');
const lightboxImage = lightbox?.querySelector('img');
document.querySelectorAll('.shot, .showcase-shot').forEach((shot) => {
  shot.addEventListener('click', () => {
    if (!lightbox || !lightboxImage) return;
    lightboxImage.src = shot.dataset.image;
    lightboxImage.alt = shot.dataset.alt;
    lightbox.showModal();
  });
});
lightbox?.querySelector('.lightbox-close')?.addEventListener('click', () => lightbox.close());
lightbox?.addEventListener('click', (event) => {
  if (event.target === lightbox) lightbox.close();
});

function preferredAsset() {
  const platform = navigator.userAgentData?.platform || navigator.platform || '';
  const value = platform.toLowerCase();
  if (value.includes('win')) return 'CyberSnapper-windows-x64.zip';
  if (value.includes('linux')) return 'CyberSnapper-linux-x64.tar.gz';
  if (value.includes('mac')) {
    return navigator.userAgent.includes('Intel') ? 'CyberSnapper-macos-x64.zip' : 'CyberSnapper-macos-arm64.zip';
  }
  return null;
}

async function resolveRelease() {
  const status = document.querySelector('#release-status');
  const hero = document.querySelector('#hero-download');
  const links = [...document.querySelectorAll('.asset-link')];
  try {
    const response = await fetch(api, { headers: { Accept: 'application/vnd.github+json' } });
    if (!response.ok) throw new Error(`GitHub returned ${response.status}`);
    const releases = await response.json();
    const release = releases.find((item) => !item.draft && !item.prerelease && /^v2\./.test(item.tag_name));
    if (!release) throw new Error('No CyberSnapper 2 release is published yet');
    const assets = new Map(release.assets.map((asset) => [asset.name, asset.browser_download_url]));
    links.forEach((link) => {
      const download = assets.get(link.dataset.asset);
      if (download) {
        link.href = download;
        link.classList.remove('is-unavailable');
      } else {
        link.href = release.html_url;
        link.classList.add('is-unavailable');
        link.title = 'This package is not attached to the latest release';
      }
    });
    const preferred = assets.get(preferredAsset());
    if (hero) {
      hero.href = preferred || '#download';
      hero.textContent = preferred ? `Download ${release.tag_name}` : 'Choose your download';
    }
    if (status) status.textContent = `${release.name || release.tag_name} is the latest stable release.`;
  } catch (error) {
    links.forEach((link) => { link.href = `${repository}/releases`; link.classList.add('is-unavailable'); });
    if (hero) hero.href = `${repository}/releases`;
    if (status) status.textContent = 'View GitHub Releases for the newest available packages.';
  }
}

resolveRelease();
