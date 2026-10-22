// Static files only, each page rendered at build time: the landing's words are in the HTML, for search
// engines and the first paint. The app itself (the engine in its worker, the stores) starts in the browser
// only ([[lang=locale]]/+page.svelte).
export const ssr = true;
export const prerender = true;
