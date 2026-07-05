const BLOCKED_DOMAINS = [
  'doubleclick.net',
  'googlesyndication.com',
  'googleadservices.com',
  'adservice.google.com',
  'pagead2.googlesyndication.com',
  'facebook.com/tr',
  'connect.facebook.net',
  'intercom.io',
  'cookiebot.com',
  'onetrust.com',
  'optanon.com',
  'trustarc.com',
  'cookielaw.org',
  'hubspot.com',
  'pardot.com',
  'marketo.com',
  'cdn.onesignal.com',
  'pushcrew.com',
  'pushwoosh.com',
  'crazyegg.com',
  'mouseflow.com',
  'fullstory.com',
  'tawk.to',
  'livechatinc.com',
  'olark.com',
];

const BLOCKED_SUBSTRINGS = ['popup', 'affiliate'];

const HIDE_POPUPS_CSS = `
  [class*="modal"], [class*="popup"], [class*="overlay"],
  [class*="cookie"], [id*="cookie"], [class*="banner"],
  [class*="newsletter"], [class*="subscribe"],
  [class*="gdpr"], [id*="gdpr"],
  [class*="consent"], [id*="consent"],
  [class*="notification"], [class*="announcement"],
  [class*="interstitial"], [class*="layer"],
  [class*="lightbox"], [class*="fancybox"],
  iframe[src*="intercom"], iframe[src*="tawk"],
  #intercom, .intercom, #hubspot-messages,
  .fc-consent-root, .cookie-consent, .cc-window,
  .mailmunch-forms, .pum-overlay, .mfp-wrap,
  [aria-modal="true"], [role="dialog"]:not([role="dialog"] form) {
    display: none !important;
    visibility: hidden !important;
    opacity: 0 !important;
    pointer-events: none !important;
    height: 0 !important;
    width: 0 !important;
    overflow: hidden !important;
    clip: rect(0,0,0,0) !important;
    position: absolute !important;
  }
`;

function isBlocked(url) {
  return BLOCKED_DOMAINS.some(d => url.includes(d)) ||
    BLOCKED_SUBSTRINGS.some(s => url.includes(s));
}

function attachPopupBlocker(page) {
  return page.route('**/*', route => {
    if (isBlocked(route.request().url())) {
      route.abort();
    } else {
      route.continue();
    }
  });
}

module.exports = { HIDE_POPUPS_CSS, attachPopupBlocker };
