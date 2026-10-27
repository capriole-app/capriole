import { expect, test, type Page } from '@playwright/test';

// The app on a phone (360 px, the narrowest common): the bar on top on one line, the commands along the
// bottom, the page as wide as the screen, its details under it only when asked for or with something to edit.

test.use({ viewport: { width: 360, height: 740 }, isMobile: true, hasTouch: true });

/** `n` A4 pages with nothing on them but, on the first, `links` links. */
function pdf(n: number, links = 0): Buffer {
  const kids = Array.from({ length: n }, (_, i) => `${3 + i} 0 R`).join(' ');
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    `<</Type/Pages/Kids[${kids}]/Count ${n}/MediaBox[0 0 595 842]>>`,
    ...Array.from({ length: n }, (_, i) => {
      const annots = Array.from(
        { length: i === 0 ? links : 0 },
        (_, k) =>
          `<</Type/Annot/Subtype/Link/Rect[50 ${800 - 20 * k} 150 ${815 - 20 * k}]/A<</S/URI/URI(https://example.com/${k})>>>>`,
      ).join('');
      return `<</Type/Page/Parent 2 0 R${annots ? `/Annots[${annots}]` : ''}>>`;
    }),
  ];
  let text = '%PDF-1.7\n';
  const offsets: number[] = [];
  bodies.forEach((body, i) => {
    offsets.push(text.length);
    text += `${i + 1} 0 obj\n${body}\nendobj\n`;
  });
  const xref = text.length;
  text += `xref\n0 ${bodies.length + 1}\n0000000000 65535 f \n`;
  text += offsets.map((o) => `${String(o).padStart(10, '0')} 00000 n \n`).join('');
  text += `trailer\n<</Size ${bodies.length + 1}/Root 1 0 R>>\nstartxref\n${xref}\n%%EOF\n`;
  return Buffer.from(text, 'latin1');
}

async function open(page: Page, lang = 'it', links = 0) {
  await page.goto(`/${lang}/`);
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'a.pdf', mimeType: 'application/pdf', buffer: pdf(3, links) });
  await expect(page.getByTestId('page')).toHaveCount(3);
}

/** Nothing wider than the screen: the page does not scroll sideways. */
const sideways = (page: Page) => page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);

test('the pages: one line on top, the actions along the bottom, the rest in ⋯', async ({ page }) => {
  await open(page);
  const top = page.locator('header:visible').first();
  expect((await top.boundingBox())!.height).toBeLessThan(60);
  const bottom = (await page.getByTestId('bottom-bar').boundingBox())!;
  expect(bottom.y + bottom.height).toBe(740);
  await expect(page.getByRole('button', { name: 'Ruota a destra' })).toBeVisible();
  expect(await sideways(page)).toBe(0);
  // Two columns of pages.
  const [first, second] = await Promise.all(
    [0, 1].map(async (i) => (await page.getByTestId('page').nth(i).boundingBox())!),
  );
  expect(second!.y).toBe(first!.y);

  await page.getByTestId('more-menu').tap();
  await expect(page.getByTestId('more-phone')).toBeVisible();
  await page.getByRole('menuitem', { name: 'Allegati' }).tap();
  // The panel over the whole screen, with its ✕.
  const panel = (await page.locator('aside').boundingBox())!;
  expect(panel.width).toBe(360);
  await page.locator('aside').getByRole('button', { name: 'Chiudi gli allegati' }).tap();
  await expect(page.locator('aside')).toHaveCount(0);
});

test('a page: as wide as the screen, its details under it when there is something to edit', async ({ page }) => {
  await open(page, 'de', 30);
  await page.getByTestId('page').first().dblclick();
  const sheet = page.getByTestId('page-sheet');
  await expect(sheet).toBeVisible();
  expect((await sheet.boundingBox())!.width).toBeGreaterThan(330);
  expect((await page.locator('header:visible').first().boundingBox())!.height).toBeLessThan(60);
  expect(await sideways(page)).toBe(0);
  const details = page.getByTestId('page-details');
  await expect(details).toBeHidden();

  // A note: its comment to write, under the page; the ✕ closes it.
  await page.locator('[data-tool=note]').tap();
  const box = (await sheet.boundingBox())!;
  await page.getByTestId('page-surface').tap({ position: { x: box.width / 2, y: box.height / 3 } });
  await expect(details).toBeVisible();
  await expect(details.locator('textarea')).toBeVisible();
  // Only what there is to edit: not the page's 30 links.
  await expect(details.getByTestId('annotation-lists')).toBeHidden();
  await details.getByRole('button', { name: 'Details schließen' }).tap();
  await expect(details).toBeHidden();

  // By hand, from ⋯: the page's annotations.
  await page.getByTestId('more-menu').tap();
  await page.getByTestId('open-details').tap();
  await expect(details).toBeVisible();
  await expect(details.getByTestId('annotation-list')).toBeVisible();
  // The links folded in one line, open on a tap.
  const folded = details.getByTestId('link-list');
  await expect(folded.locator('summary')).toHaveText('30 Links');
  await expect(folded.getByRole('button').first()).toBeHidden();
  await folded.locator('summary').tap();
  await expect(folded.getByRole('button')).toHaveCount(30);
  await folded.locator('summary').tap();

  // The colour's menu opens above its button, at the bottom.
  await page.getByTestId('style-menu').tap();
  const menu = (await page.getByRole('dialog', { name: 'Farbe und Textgröße' }).boundingBox())!;
  const button = (await page.getByTestId('style-menu').boundingBox())!;
  expect(menu.y).toBeGreaterThan(0);
  expect(menu.y + menu.height).toBeLessThanOrEqual(button.y);
});

test('a finger scrolls the page while selecting, and draws with the drawing tools', async ({ page }) => {
  await open(page);
  await page.getByTestId('page').first().dblclick();
  const surface = page.getByTestId('page-surface');
  await expect(surface).toHaveCSS('touch-action', 'pan-x pan-y');
  await page.locator('[data-tool=ink]').tap();
  await expect(surface).toHaveCSS('touch-action', 'none');
});
