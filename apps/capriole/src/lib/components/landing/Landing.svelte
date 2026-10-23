<script lang="ts">
  // The page without a file: what search engines read (rendered at build time, in the page's language) and
  // where a person opens a PDF. The sections follow the brief (MARKETING/capriole/LANDING-brief.md, outside
  // the repository, «Versione 2»). In the desktop shell only the card (data-web-only, desktop:…: app.css).
  import Desktop from './Desktop.svelte';
  import Faq from './Faq.svelte';
  import Features from './Features.svelte';
  import Footer from './Footer.svelte';
  import FromScratch from './FromScratch.svelte';
  import Gallery from './Gallery.svelte';
  import Header from './Header.svelte';
  import Hero from './Hero.svelte';
  import Privacy from './Privacy.svelte';
</script>

<Header />
<main>
  <Hero />
  <div data-web-only>
    <Privacy />
    <FromScratch />
    <Features />
    <Gallery />
    <Desktop />
    <Faq />
  </div>
</main>
<Footer />
