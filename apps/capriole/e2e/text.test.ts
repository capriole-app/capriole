import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { withoutFileAccess } from './downloads';

/** One blank page, 300 × 400 points. */
function pdf(): Buffer {
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    '<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 400]>>',
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

/** A point of the page (points as shown) on the screen. */
async function at(page: Page, x: number, y: number): Promise<{ x: number; y: number }> {
  const box = (await page.getByTestId('page-surface').boundingBox())!;
  return { x: box.x + (x * box.width) / 300, y: box.y + (y * box.height) / 400 };
}

/** The text box's size in points of the page. */
async function size(page: Page): Promise<{ width: number; height: number }> {
  const surface = (await page.getByTestId('page-surface').boundingBox())!;
  const box = (await page.getByTestId('text-box').boundingBox())!;
  return { width: (box.width * 300) / surface.width, height: (box.height * 400) / surface.height };
}

test.beforeEach(({ page }) => withoutFileAccess(page));

test('free text is written on the page: a click widens, a rectangle wraps and grows, a double click writes again', async ({
  page,
}) => {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.getByTestId('open-file').setInputFiles({ name: 'testo.pdf', mimeType: 'application/pdf', buffer: pdf() });
  await page.getByTestId('page').first().dblclick();
  const list = page.getByTestId('annotation-list');
  await page.getByRole('button', { name: 'Testo', exact: true }).click();
  await page.getByTestId('style-menu').click();
  await page.getByRole('combobox', { name: 'Dimensione del testo' }).selectOption('18');

  // A click: a box one line of 18 points high, the cursor in it; it widens as the text is typed.
  const spot = await at(page, 30, 40);
  await page.mouse.click(spot.x, spot.y);
  const box = page.getByTestId('text-box');
  await expect(box).toBeFocused();
  const empty = await size(page);
  expect(empty.height).toBeGreaterThan(18 * 1.2 + 3);
  expect(empty.height).toBeLessThan(18 * 1.2 + 6);
  await page.keyboard.type('Una riga sola');
  const typed = await size(page);
  expect(typed.width).toBeGreaterThan(empty.width + 60);
  expect(typed.height).toBeCloseTo(empty.height, 0);
  await page.keyboard.press('Escape');
  await expect(list).toContainText('Testo — Una riga sola');

  // A rectangle 100 points wide: lines wrap at its edge, it grows downwards, its width stays.
  await page.getByTestId('style-menu').click();
  await page.getByRole('combobox', { name: 'Dimensione del testo' }).selectOption('12');
  const from = await at(page, 30, 120);
  const to = await at(page, 130, 130);
  await page.mouse.move(from.x, from.y);
  await page.mouse.down();
  await page.mouse.move(to.x, to.y, { steps: 5 });
  await page.mouse.up();
  await expect(box).toBeFocused();
  const rectangle = await size(page);
  await page.keyboard.type('Un testo lungo che non entra in una riga sola della casella');
  const wrapped = await size(page);
  expect(wrapped.width).toBeCloseTo(rectangle.width, 0);
  expect(wrapped.height).toBeGreaterThan(rectangle.height + 12);
  // A click elsewhere ends it.
  const away = await at(page, 250, 380);
  await page.mouse.click(away.x, away.y);
  await expect(list).toContainText('Testo — Un testo lungo');

  // A double click on the first text writes it again; the whole writing is one step of undo.
  await page.getByRole('button', { name: 'Seleziona e sposta' }).click();
  const first = await at(page, 40, 40);
  await page.mouse.dblclick(first.x, first.y);
  await expect(box).toBeFocused();
  await expect(box).toHaveText('Una riga sola');
  await page.keyboard.type(', due');
  await page.keyboard.press('Escape');
  await expect(list).toContainText('Testo — Una riga sola, due');
  await page.getByRole('button', { name: 'Annulla' }).click();
  await expect(list).toContainText('Testo — Una riga sola');
  await expect(list).not.toContainText('Una riga sola, due');

  // Emptied, it is removed.
  await page.mouse.dblclick(first.x, first.y);
  await expect(box).toBeFocused();
  await page.keyboard.press('Control+A');
  await page.keyboard.press('Delete');
  await page.keyboard.press('Escape');
  await expect(list).not.toContainText('Una riga sola');

  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Salva' }).click();
  const saved = readFileSync(await (await download).path()).toString('latin1');
  expect(saved).toMatch(/\/Subtype \/FreeText/);
  expect(saved).toMatch(/\/Contents \(Un testo lungo che non entra in una riga sola della casella\)/);
  expect(saved).toMatch(/\/DA \(\/Helv 12 Tf/);
  expect(saved).not.toMatch(/Una riga sola/);
});
