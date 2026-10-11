import { expect, test, type Page } from '@playwright/test';

/** Pages of 300 × 400 points, each with a line of text. */
function pdf(pages: number): Buffer {
  const kids = Array.from({ length: pages }, (_, i) => `${3 + 2 * i} 0 R`).join(' ');
  const bodies = ['<</Type/Catalog/Pages 2 0 R>>', `<</Type/Pages/Kids[${kids}]/Count ${pages}>>`];
  for (let i = 0; i < pages; i++) {
    const content = `BT /F1 24 Tf 30 300 Td (Pagina ${i + 1}) Tj ET`;
    bodies.push(
      `<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 400]/Contents ${4 + 2 * i} 0 R/Resources<</Font<</F1<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>>>>>>>`,
      `<</Length ${content.length}>>\nstream\n${content}\nendstream`,
    );
  }
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

async function open(page: Page) {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.getByTestId('open-file').setInputFiles({ name: 'zoom.pdf', mimeType: 'application/pdf', buffer: pdf(3) });
  await expect(page.getByTestId('page')).toHaveCount(3);
}

const width = async (page: Page, testId: string) => (await page.getByTestId(testId).first().boundingBox())!.width;

test('the pages grid zooms: − and +, a percentage written by hand, Ctrl + wheel; remembered', async ({ page }) => {
  await open(page);
  const zoom = page.getByTestId('zoom');
  const percent = zoom.getByLabel('Percentuale di zoom');
  await expect(percent).toHaveValue('100%');
  await expect(zoom.getByRole('button', { name: 'Adatta' })).toHaveCount(0); // only for one page
  const before = await width(page, 'page');

  await zoom.getByRole('button', { name: 'Ingrandisci' }).click();
  await expect(percent).toHaveValue('110%');
  await zoom.getByRole('button', { name: 'Riduci' }).click();
  await expect(percent).toHaveValue('100%');

  await percent.fill('200');
  await percent.press('Enter');
  await expect(percent).toHaveValue('200%');
  await expect.poll(() => width(page, 'page')).toBeGreaterThan(before * 1.6);

  // Not a number: the zoom stays. Beyond the range: its end.
  await percent.fill('molto');
  await percent.press('Enter');
  await expect(percent).toHaveValue('200%');
  await percent.fill('1000 %');
  await percent.press('Enter');
  await expect(percent).toHaveValue('300%');

  // Ctrl + wheel over the pages; Ctrl + 0 back to the usual size.
  await page.getByTestId('page').first().hover();
  await page.keyboard.down('Control');
  await page.mouse.wheel(0, 100);
  await page.keyboard.up('Control');
  await expect(percent).toHaveValue('250%');
  await page.keyboard.press('Control+0');
  await expect(percent).toHaveValue('100%');

  // Remembered on the device.
  await percent.fill('150');
  await percent.press('Enter');
  await open(page);
  await expect(page.getByTestId('zoom').getByLabel('Percentuale di zoom')).toHaveValue('150%');
});

test('one page zooms: fit to the window, a percentage, − and +, and back to fit', async ({ page }) => {
  await open(page);
  await page.getByTestId('page').first().dblclick();
  const zoom = page.getByTestId('zoom');
  const fit = zoom.getByRole('button', { name: 'Adatta' });
  const percent = zoom.getByLabel('Percentuale di zoom');
  await expect(fit).toHaveAttribute('aria-pressed', 'true');
  const fitted = await width(page, 'page-sheet');
  // Fitted, the percentage says the size the page has: 100% is 300 points = 400 pixels.
  const shown = Number((await percent.inputValue()).replace('%', ''));
  expect(Math.abs(shown - (fitted / 400) * 100)).toBeLessThanOrEqual(1);

  await percent.fill('300');
  await percent.press('Enter');
  await expect(fit).toHaveAttribute('aria-pressed', 'false');
  await expect.poll(() => width(page, 'page-sheet')).toBeCloseTo(1200, 0);
  // Larger than the window: it scrolls.
  const main = page.locator('main').filter({ has: page.getByTestId('page-sheet') });
  expect(await main.evaluate((m) => m.scrollWidth > m.clientWidth || m.scrollHeight > m.clientHeight)).toBe(true);

  await zoom.getByRole('button', { name: 'Riduci' }).click();
  await expect(percent).toHaveValue('250%');
  await expect.poll(() => width(page, 'page-sheet')).toBeCloseTo(1000, 0);

  await fit.click();
  await expect(fit).toHaveAttribute('aria-pressed', 'true');
  await expect.poll(() => width(page, 'page-sheet')).toBeCloseTo(fitted, 0);

  // Ctrl + wheel zooms the page from the size it has.
  await page.getByTestId('page-surface').hover();
  await page.keyboard.down('Control');
  await page.mouse.wheel(0, -100);
  await page.keyboard.up('Control');
  await expect(fit).toHaveAttribute('aria-pressed', 'false');
  await expect.poll(() => width(page, 'page-sheet')).toBeGreaterThan(fitted);
});
