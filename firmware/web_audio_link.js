// Loaded by ESPHome's own device page (web_server js_include, served as /0.js). It makes the "Change Sounds At"
// value a link to the /audio page. ESPHome draws entity values as plain text, so this only styles that value and
// handles its click: it never replaces ESPHome's elements, which ESPHome redraws whenever a state changes.
// The /audio page itself stays plain HTML without JavaScript.
const isAudioLink = (el) =>
  el instanceof HTMLElement && el.childElementCount === 0 && /^https?:\/\/\S+\/audio$/.test(el.textContent.trim());

const mark = (root) => {
  for (const el of root.querySelectorAll("*")) {
    if (el.shadowRoot) mark(el.shadowRoot);
    if (isAudioLink(el) && !el.dataset.audioLink) {
      el.dataset.audioLink = "1";
      el.style.color = "#0b62d6";
      el.style.textDecoration = "underline";
      el.style.cursor = "pointer";
    }
  }
};
setInterval(() => mark(document), 1000);

document.addEventListener(
  "click",
  (e) => {
    if (isAudioLink(e.composedPath()[0])) {
      e.preventDefault();
      e.stopPropagation();
      location.href = "/audio";
    }
  },
  true,
);
