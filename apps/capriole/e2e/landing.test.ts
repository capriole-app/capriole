import { expect, test, type Page } from '@playwright/test';

// The page without a file, rendered at build time: its words are in the HTML (what search engines read,
// before any script), and the app takes it over in the browser.

const empty = Buffer.from(
  '%PDF-1.7\n1 0 obj\n<</Type/Catalog/Pages 2 0 R>>\nendobj\n2 0 obj\n<</Type/Pages/Kids[3 0 R]/Count 1>>\nendobj\n3 0 obj\n<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 200]>>\nendobj\ntrailer\n<</Root 1 0 R>>\n%%EOF\n',
  'latin1',
);

const taglines = {
  '/': 'Edit, sign and fill in PDFs. Free, no account.',
  '/it/': 'Modifica, firma e compila i PDF. Gratis, senza account.',
  '/en/': 'Edit, sign and fill in PDFs. Free, no account.',
  '/fr/': 'Modifiez, signez et remplissez vos PDF. Gratuit, sans compte.',
  '/de/': 'PDFs bearbeiten, signieren und ausfüllen. Kostenlos, ohne Konto.',
  '/es/': 'Edita, firma y rellena PDF. Gratis, sin cuenta.',
};

for (const [path, tagline] of Object.entries(taglines)) {
  test(`${path}: the landing's words are in the HTML`, async ({ request }) => {
    const html = await (await request.get(path)).text();
    expect(html).toContain(tagline);
    expect(html).toContain('aria-label="Capriole"');
    // The logo's file is asked for by the app running, not by the page: it counts the starts.
    expect(html).not.toContain('/logo-light.svg');
  });
}

// A few words of each section, per language (the words' module needs Svelte's runes, not loaded here).
const sections = {
  it: [
    'anche il testo',
    'Neanche un byte del documento lascia il tuo computer.',
    'Redaction vera',
    'Da dove viene Capriole?',
  ],
  en: [
    'the text too',
    'Not a single byte of the document leaves your computer.',
    'Real redaction',
    'Where does Capriole come from?',
  ],
  fr: [
    'texte compris',
    'Pas un octet du document ne quitte votre ordinateur.',
    'Vraie suppression',
    'D’où vient Capriole ?',
  ],
  de: [
    'auch den Text',
    'Kein einziges Byte des Dokuments verlässt Ihren Computer.',
    'Echtes Schwärzen',
    'Woher kommt Capriole?',
  ],
  es: [
    'también el texto',
    'Ni un byte del documento sale de tu ordenador.',
    'Ocultación real',
    '¿De dónde viene Capriole?',
  ],
};

for (const [lang, words] of Object.entries(sections))
  test(`/${lang}/: the landing's sections are in the HTML`, async ({ request }) => {
    const html = await (await request.get(`/${lang}/`)).text();
    for (const w of words) expect(html, w).toContain(w);
    for (const id of ['top', 'privacy', 'from-scratch', 'features', 'desktop', 'faq'])
      expect(html).toContain(`id="${id}"`);
    expect(html.match(/<details/g)).toHaveLength(6);
    expect(html).toContain(`/landing/${lang}/text-light.webp`);
    expect(html.match(/<h1[\s>]/g)).toHaveLength(1);
  });

/** A file dropped on the page at `selector`, far from the card. */
async function drop(page: Page, selector: string) {
  const data = await page.evaluateHandle(
    (bytes) => {
      const transfer = new DataTransfer();
      transfer.items.add(new File([new Uint8Array(bytes)], 'vuoto.pdf', { type: 'application/pdf' }));
      return transfer;
    },
    [...empty],
  );
  await page.dispatchEvent(selector, 'dragenter', { dataTransfer: data });
  await page.dispatchEvent(selector, 'dragover', { dataTransfer: data });
  await page.dispatchEvent(selector, 'drop', { dataTransfer: data });
}

for (const viewport of [
  { width: 1440, height: 900 },
  { width: 375, height: 667 },
])
  test(`at ${viewport.width}×${viewport.height} the card is on the first screen, unscrolled`, async ({ page }) => {
    await page.setViewportSize(viewport);
    await page.goto('/it/');
    await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeAttached();
    await expect(page.getByRole('heading', { level: 1 })).toBeInViewport();
    await expect(page.getByRole('button', { name: 'Scegli un PDF' })).toBeInViewport({ ratio: 1 });
  });

test('a PDF dropped anywhere on the page opens', async ({ page }) => {
  await page.goto('/it/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await drop(page, '#faq');
  await expect(page.getByTestId('page')).toHaveCount(1);
});

test.describe('without the terms accepted', () => {
  test.use({ storageState: { cookies: [], origins: [] } });
  test('a PDF dropped far from the card brings the card back, asking for the box', async ({ page }) => {
    await page.goto('/it/');
    await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
    await page.locator('footer').scrollIntoViewIfNeeded();
    await drop(page, 'footer');
    await expect(page.getByRole('alert')).toHaveText('Per aprire un file, accetta prima le condizioni d’uso.');
    await expect(page.getByTestId('accept-terms')).toBeInViewport();
    await expect(page.getByTestId('page')).toHaveCount(0);
  });
});

test('the gallery shows each scene, in the page’s language and theme', async ({ page }) => {
  await page.emulateMedia({ colorScheme: 'dark' });
  await page.goto('/en/');
  const tabs = page.getByRole('tablist').getByRole('tab');
  await expect(tabs).toHaveText(['Pages', 'Text', 'Annotations', 'Signature']);
  const shot = page.getByRole('tabpanel').locator('img:visible');
  await expect(shot).toHaveAttribute('src', '/landing/en/pages-dark.webp');
  await tabs.nth(0).focus();
  await page.keyboard.press('ArrowRight');
  await expect(tabs.nth(1)).toBeFocused();
  await expect(tabs.nth(1)).toHaveAttribute('aria-selected', 'true');
  await expect(shot).toHaveAttribute('src', '/landing/en/text-dark.webp');
  await tabs.nth(3).click();
  await expect(shot).toHaveAttribute('src', '/landing/en/signature-dark.webp');
  await expect(shot).toHaveJSProperty('complete', true);
  expect(await shot.evaluate((img: HTMLImageElement) => img.naturalWidth)).toBe(1600);
});

test('the app takes the landing over: the logo from its file, a file opened from the card', async ({ page }) => {
  const logo = page.waitForRequest((r) => r.url().endsWith('/logo-light.svg'));
  await page.goto('/it/');
  await logo;
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.getByTestId('open-file').setInputFiles({
    name: 'vuoto.pdf',
    mimeType: 'application/pdf',
    buffer: Buffer.from(
      '%PDF-1.7\n1 0 obj\n<</Type/Catalog/Pages 2 0 R>>\nendobj\n2 0 obj\n<</Type/Pages/Kids[3 0 R]/Count 1>>\nendobj\n3 0 obj\n<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 200]>>\nendobj\ntrailer\n<</Root 1 0 R>>\n%%EOF\n',
      'latin1',
    ),
  });
  await expect(page.getByTestId('page')).toHaveCount(1);
  await expect(page.getByRole('region', { name: 'Apri un PDF' })).toHaveCount(0);
  await expect(page.getByRole('heading', { level: 1 })).toHaveCount(0);
});

test('in the desktop shell what only the web app has is hidden from the first paint', async ({ page }) => {
  await page.goto('/it/');
  await expect(page.getByTestId('download-open')).toBeVisible();
  // app.html marks the shell on <html> before anything paints; here the mark alone, on a browser.
  await page.evaluate(() => document.documentElement.classList.add('desktop'));
  await expect(page.getByTestId('download-open')).toBeHidden();
  await expect(page.getByTestId('report-button').locator('visible=true')).toHaveCount(0);
  // The card alone, with its own logo, buttons and links; nothing of the landing around it.
  await expect(page.getByRole('heading', { level: 1 })).toBeHidden();
  await expect(page.locator('#faq')).toBeHidden();
  await expect(page.locator('footer')).toBeHidden();
  const card = page.getByRole('region', { name: 'Apri un PDF' });
  await expect(card.getByRole('img', { name: 'Capriole' }).first()).toBeVisible();
  await expect(card.getByRole('button', { name: 'Lingua' })).toBeVisible();
  await expect(card.getByRole('button', { name: 'Informazioni e licenze' })).toBeVisible();
  await expect(card.getByText('Il file resta sul tuo computer.')).toBeVisible();
  await expect(card.getByText('nessun byte va su un server')).toBeHidden();
});

test('the desktop section links the installers of the latest release', async ({ page }) => {
  await page.goto('/en/');
  const links = page.locator('#desktop').getByTestId('landing-download');
  const latest = 'https://github.com/capriole-app/capriole/releases/latest/download/';
  await expect(links).toHaveCount(3);
  for (const [i, file] of ['Capriole_x64-setup.exe', 'Capriole_amd64.deb', 'Capriole_amd64.AppImage'].entries())
    await expect(links.nth(i)).toHaveAttribute('href', latest + file);
});
