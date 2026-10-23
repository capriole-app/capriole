import { expect, test, type Page } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { contract, edit, fileName, height, termParagraph, width, type Lang, type Spot } from './samples';

// The landing's screenshots (npm run shots; playwright.shots.config.ts): the built app in a browser, a
// contract invented for the purpose (samples.ts) in each language and theme, brought to a scene and
// captured, then made WebP by the browser itself (no image library). Into static/landing/<lang>/, and the
// Open Graph image of each language into static/og/. Not part of the gate: run again when the interface
// changes, and commit what it writes.
//
// The interface is driven by what does not depend on the language (data-tool, data-mode, test ids, the
// pages' positions), so that one scene serves the five.

const root = fileURLToPath(new URL('..', import.meta.url));
const langs: Lang[] = ['it', 'en', 'fr', 'de', 'es'];
const themes = ['light', 'dark'] as const;
const viewport = { width: 1600, height: 1000 };
/** The signature scene's certificate, with a name as a person's (make_certificate.sh): the tests' own say "(test)". */
const certificate = `${root}/test-results/anna-bianchi.p12`;

test.beforeAll(() => {
  execFileSync(`${root}/shots/make_certificate.sh`, [`${root}/test-results`]);
});

/** The WebP of a PNG, encoded by the page's browser (canvas.toBlob). */
async function webp(page: Page, png: Buffer, quality = 0.82): Promise<Buffer> {
  const base64 = await page.evaluate(
    async ({ data, quality }) => {
      const image = new Image();
      image.src = `data:image/png;base64,${data}`;
      await image.decode();
      const canvas = document.createElement('canvas');
      canvas.width = image.naturalWidth;
      canvas.height = image.naturalHeight;
      canvas.getContext('2d')!.drawImage(image, 0, 0);
      const blob = await new Promise<Blob>((resolve, reject) =>
        canvas.toBlob((b) => (b ? resolve(b) : reject(new Error('no WebP'))), 'image/webp', quality),
      );
      const bytes = new Uint8Array(await blob.arrayBuffer());
      let binary = '';
      for (const b of bytes) binary += String.fromCharCode(b);
      return btoa(binary);
    },
    { data: png.toString('base64'), quality },
  );
  return Buffer.from(base64, 'base64');
}

/** A point of the page shown large, given in points from the page's bottom-left corner. */
async function at(page: Page, x: number, y: number): Promise<{ x: number; y: number }> {
  const box = (await page.getByTestId('page-surface').boundingBox())!;
  return { x: box.x + (x * box.width) / width, y: box.y + ((height - y) * box.height) / height };
}

async function drag(page: Page, from: [number, number], to: [number, number]) {
  const a = await at(page, ...from);
  const b = await at(page, ...to);
  await page.mouse.move(a.x, a.y);
  await page.mouse.down();
  await page.mouse.move((a.x + b.x) / 2, (a.y + b.y) / 2, { steps: 6 });
  await page.mouse.move(b.x, b.y, { steps: 6 });
  await page.mouse.up();
}

/** Opens the contract in `lang`, the app in that language, and waits for its pages. */
async function open(page: Page, lang: Lang) {
  // Saving would be a download: no scene saves, and the system's dialogs are kept out of the way.
  await page.addInitScript(() => {
    delete (window as { showOpenFilePicker?: unknown }).showOpenFilePicker;
    delete (window as { showSaveFilePicker?: unknown }).showSaveFilePicker;
  });
  await page.goto(`/${lang}/`);
  await expect(page.getByText(/\d+\.\d+\.\d+ \(WebAssembly\)/)).toBeVisible();
  await page.getByTestId('open-file').setInputFiles({
    name: fileName[lang],
    mimeType: 'application/pdf',
    buffer: contract(lang).pdf,
  });
  await expect(page.getByTestId('page')).toHaveCount(7);
}

/** The zoom written in the bar's field (the grid's or the page's). */
async function zoom(page: Page, percent: number) {
  const field = page.getByTestId('zoom').locator('input');
  await field.fill(`${percent}%`);
  await field.press('Enter');
}

/** The page at `index` shown large, closer than fitted (its words read in the screenshot), or fitted. */
async function view(page: Page, index: number, closer = true) {
  await page.getByTestId('page').nth(index).dblclick();
  await expect(page.getByTestId('page-surface')).toBeVisible();
  if (closer) await zoom(page, 115);
}

/** Waits for the page's annotations to number `n` (each one is added by the engine, a moment later). */
async function annotations(page: Page, n: number) {
  await expect(page.getByTestId('annotation-list').getByRole('listitem')).toHaveCount(n);
}

async function save(page: Page, lang: Lang, theme: string, name: string) {
  // The pages drawn, the fonts in, the caret still: then the picture.
  await page.evaluate(() => document.fonts.ready);
  await page.mouse.move(0, 0);
  await page.waitForTimeout(400);
  const png = await page.screenshot({ animations: 'disabled', caret: 'hide' });
  const dir = `${root}/static/landing/${lang}`;
  mkdirSync(dir, { recursive: true });
  writeFileSync(`${dir}/${name}-${theme}.webp`, await webp(page, png));
  return png;
}

const scenes: Record<string, (page: Page, lang: Lang) => Promise<void>> = {
  /** The pages: the grid, two of them selected. */
  async pages(page) {
    await zoom(page, 200);
    await page.getByTestId('page').nth(1).click();
    await page
      .getByTestId('page')
      .nth(2)
      .click({ modifiers: ['Shift'] });
  },
  /** The text: article 2's paragraph open in the editor, its term rewritten. */
  async text(page, lang) {
    await view(page, 0);
    await page.locator('[data-tool="edittext"]').click();
    await page.locator('[data-mode="paragraph"]').click();
    const { term } = contract(lang);
    const spot = await at(page, term.x + 60, term.y + 3);
    const box = page.getByTestId('paragraph-editor');
    await expect(async () => {
      if ((await box.count()) === 0) await page.mouse.dblclick(spot.x, spot.y);
      await expect(box).toHaveValue(termParagraph(lang), { timeout: 800 });
    }).toPass();
    await box.fill(termParagraph(lang).replace(edit[lang].from, edit[lang].to));
  },
  /** Annotations: article 3 highlighted, a note, a line drawn with the pen. */
  async annotations(page, lang) {
    await view(page, 0);
    const { fees } = contract(lang) as { fees: Spot };
    await page.locator('[data-tool="highlight"]').click();
    await drag(page, [fees.x, fees.y + 3], [fees.x + 380, fees.y + 3]);
    await annotations(page, 1);
    // A tick in the margin, beside article 2, with the pen.
    const { term } = contract(lang);
    await page.locator('[data-tool="ink"]').click();
    const tick = [
      [-40, term.y + 2],
      [-32, term.y - 6],
      [-16, term.y + 14],
    ] as const;
    const points = await Promise.all(tick.map(([dx, y]) => at(page, term.x + dx, y)));
    await page.mouse.move(points[0]!.x, points[0]!.y);
    await page.mouse.down();
    for (const p of points.slice(1)) await page.mouse.move(p.x, p.y, { steps: 6 });
    await page.mouse.up();
    await annotations(page, 2);
    await page.locator('[data-tool="note"]').click();
    const note = await at(page, 530, fees.y + 30);
    // Again until it is there: a click while the engine still adds the pen's line is not taken.
    const items = page.getByTestId('annotation-list').getByRole('listitem');
    await expect(async () => {
      if ((await items.count()) < 3) await page.mouse.click(note.x, note.y);
      await expect(items).toHaveCount(3, { timeout: 1000 });
    }).toPass();
    await page.getByTestId('annotation-panel').getByRole('textbox').fill('OK');
    await page.getByTestId('annotation-panel').getByRole('textbox').blur();
  },
  /** The signature: a box drawn on the second page, the dialog with the certificate chosen. */
  async signature(page) {
    await view(page, 1, false);
    await page.locator('[data-tool="sign"]').click();
    await drag(page, [330, 230], [520, 160]);
    await expect(page.getByRole('dialog')).toBeVisible();
    await page.getByTestId('sign-key').setInputFiles(certificate);
    const password = page.getByRole('dialog').locator('input[type="password"]');
    await password.fill('capriole');
    await password.press('Tab');
    await expect(page.getByTestId('sign-certificate')).toBeVisible();
  },
};

for (const lang of langs)
  for (const theme of themes)
    for (const [name, scene] of Object.entries(scenes))
      test(`${lang} ${theme} ${name}`, async ({ browser }) => {
        const context = await browser.newContext({ viewport, colorScheme: theme, locale: lang });
        const page = await context.newPage();
        await open(page, lang);
        await scene(page, lang);
        const png = await save(page, lang, theme, name);
        if (theme === 'light' && name === 'text') {
          mkdirSync(`${root}/test-results`, { recursive: true });
          writeFileSync(`${root}/test-results/text-${lang}.png`, png);
        }
        await context.close();
      });

/** The headline and the line under it, as the landing says them (MARKETING brief, S1). */
const og: Record<Lang, { title: string; line: string }> = {
  it: {
    title: 'Modifica i PDF online, anche il testo.',
    line: 'Gratis e open source. Il file non lascia il tuo computer.',
  },
  en: { title: 'Edit PDFs online, the text too.', line: 'Free and open source. Your file never leaves your computer.' },
  fr: {
    title: 'Modifiez vos PDF en ligne, texte compris.',
    line: 'Gratuit et open source. Le fichier ne quitte pas votre ordinateur.',
  },
  de: {
    title: 'PDFs online bearbeiten, auch den Text.',
    line: 'Kostenlos und Open Source. Die Datei verlässt Ihren Computer nicht.',
  },
  es: {
    title: 'Edita PDF en línea, también el texto.',
    line: 'Gratis y de código abierto. El archivo no sale de tu ordenador.',
  },
};

test.describe('Open Graph', () => {
  test.describe.configure({ mode: 'serial' });
  for (const lang of langs)
    test(`og ${lang}`, async ({ browser }) => {
      const context = await browser.newContext({ viewport: { width: 1200, height: 630 } });
      const page = await context.newPage();
      // The app's origin, for its logo and its font (the built CSS names the file by its hash).
      await page.goto('/en/');
      const assets = readdirSync(`${root}/build/_app/immutable/assets`);
      const font = assets.find((f) => /^inter-latin-wght-normal.*\.woff2$/.test(f));
      const shot = readFileSync(`${root}/test-results/text-${lang}.png`).toString('base64');
      const html = readFileSync(`${root}/shots/og.html`, 'utf8')
        .replace('%font%', font ? `/_app/immutable/assets/${font}` : '')
        .replace('%title%', og[lang].title)
        .replace('%line%', og[lang].line)
        .replace('%shot%', `data:image/png;base64,${shot}`);
      await page.setContent(html, { waitUntil: 'load' });
      await page.evaluate(() => document.fonts.ready);
      mkdirSync(`${root}/static/og`, { recursive: true });
      await page.screenshot({ path: `${root}/static/og/${lang}.png` });
      await context.close();
    });
});
