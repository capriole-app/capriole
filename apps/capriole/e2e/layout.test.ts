import { expect, test } from '@playwright/test';

// The interface's layout where it went wrong: the page's bar on two lines at 1440 px, the selection of a
// page not drawn yet seen only from its number.

/** `n` empty pages of 300 × 400 points. */
function pdf(n: number): Buffer {
  const kids = Array.from({ length: n }, (_, i) => `${3 + i} 0 R`).join(' ');
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    `<</Type/Pages/Kids[${kids}]/Count ${n}/MediaBox[0 0 300 400]>>`,
    ...Array.from({ length: n }, () => '<</Type/Page/Parent 2 0 R>>'),
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

for (const lang of ['it', 'en', 'fr', 'de', 'es']) {
  test(`the page's bar is one line at 1440 px, in ${lang}`, async ({ page }) => {
    await page.setViewportSize({ width: 1440, height: 900 });
    await page.goto(`/${lang}/`);
    await page.getByTestId('open-file').setInputFiles({ name: 'a.pdf', mimeType: 'application/pdf', buffer: pdf(2) });
    await page.getByTestId('page').first().dblclick();
    const bar = page.locator('header:visible').first();
    await expect(page.getByTestId('style-menu')).toBeVisible();
    // One line of 36 px buttons, and room left for the form's tool (a document with fields has it).
    expect((await bar.boundingBox())!.height).toBeLessThan(60);
    const room = await bar.evaluate((header) => {
      const [before, last] = [...header.children].slice(-2).map((e) => e.getBoundingClientRect());
      return last.left - before.right;
    });
    expect(room).toBeGreaterThan(32 + 2 + 8);
  });
}

test('a page selected before it is drawn shows its selection', async ({ page }) => {
  await page.goto('/');
  await page.getByTestId('open-file').setInputFiles({ name: 'a.pdf', mimeType: 'application/pdf', buffer: pdf(60) });
  await expect(page.getByTestId('page')).toHaveCount(60);
  await page.keyboard.press('ControlOrMeta+a');
  // The last page is far below: not drawn, its placeholder carries the blue line.
  const last = page.getByTestId('page').last();
  await expect(last).toHaveAttribute('aria-pressed', 'true');
  await expect(last.locator('canvas')).toHaveCount(0);
  await expect(last.locator('span.outline-emphasis')).toHaveCount(1);
});
