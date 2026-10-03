import { expect, test, type Page } from '@playwright/test';
import { withoutFileAccess } from './downloads';
import { readFileSync } from 'node:fs';

/** Two pages of 300 × 400 points; "Uno" in 36 pt Helvetica at (40, 200), i.e. y ≈ 174–208 as shown. */
function pdf(): Buffer {
  const labels = ['Uno', 'Due'];
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R 4 0 R]/Count 2/MediaBox[0 0 300 400]>>',
    ...labels.map(
      (_, i) =>
        `<</Type/Page/Parent 2 0 R/Contents ${5 + i} 0 R/Resources<</Font<</F1<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>>>>>>>`,
    ),
    ...labels.map((label) => {
      const content = `BT /F1 36 Tf 40 200 Td (${label}) Tj ET`;
      return `<</Length ${content.length}>>\nstream\n${content}\nendstream`;
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

/** A point of the page (points as shown) on the screen. */
async function at(page: Page, x: number, y: number): Promise<{ x: number; y: number }> {
  const box = (await page.getByTestId('page-surface').boundingBox())!;
  return { x: box.x + (x * box.width) / 300, y: box.y + (y * box.height) / 400 };
}

async function dragOnPage(page: Page, from: [number, number], to: [number, number]) {
  const a = await at(page, ...from);
  const b = await at(page, ...to);
  await page.mouse.move(a.x, a.y);
  await page.mouse.down();
  await page.mouse.move((a.x + b.x) / 2, (a.y + b.y) / 2, { steps: 4 });
  await page.mouse.move(b.x, b.y, { steps: 4 });
  await page.mouse.up();
}

// Saving here is a download (the path of browsers without file access).
test.beforeEach(({ page }) => withoutFileAccess(page));

test('annotates a page: highlight, note, rectangle; moves, deletes, undoes, saves', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.getByTestId('open-file').setInputFiles({ name: 'note.pdf', mimeType: 'application/pdf', buffer: pdf() });
  await expect(page.getByTestId('page')).toHaveCount(2);

  // The first page, large.
  await page.getByTestId('page').first().dblclick();
  await expect(page.getByText('Pagina 1 di 2')).toBeVisible();
  const list = page.getByTestId('annotation-list');

  // A highlight, dragging over "Uno".
  await page.getByRole('button', { name: 'Evidenzia' }).click();
  await dragOnPage(page, [45, 190], [100, 190]);
  await expect(list).toContainText('Evidenziazione');

  // A note, its comment typed in the panel.
  await page.getByRole('button', { name: 'Nota' }).click();
  const spot = await at(page, 250, 30);
  await page.mouse.click(spot.x, spot.y);
  const panel = page.getByTestId('annotation-panel');
  await expect(panel.getByRole('heading', { name: 'Nota' })).toBeVisible();
  await panel.getByRole('textbox').fill('Ciao');
  await panel.getByRole('textbox').blur();
  await expect(list).toContainText('Nota — Ciao');

  // A rectangle in blue, then moved 20 points right with the selection tool.
  await page.getByRole('radio', { name: 'Blu' }).first().click();
  await page.getByRole('button', { name: 'Rettangolo' }).click();
  await dragOnPage(page, [50, 250], [150, 320]);
  await expect(list).toContainText('Rettangolo');
  await page.getByRole('button', { name: 'Seleziona e sposta' }).click();
  await dragOnPage(page, [100, 252], [120, 252]);

  // The note removed with Delete, and back with undo.
  await list.getByText('Nota — Ciao').click();
  await page.keyboard.press('Delete');
  await expect(list).not.toContainText('Nota');
  await page.getByRole('button', { name: 'Annulla' }).click();
  await expect(list).toContainText('Nota — Ciao');

  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Salva' }).click();
  const saved = readFileSync(await (await download).path()).toString('latin1');
  expect(saved).toMatch(/\/Subtype \/Highlight/);
  expect(saved).toMatch(/\/Subtype \/Text/);
  expect(saved).toMatch(/\/Contents \(Ciao\)/);
  expect(saved).toMatch(/\/Subtype \/Square[^]*?\/Rect \[70 80 170 150\]/); // moved: x +20 (page space: y from the bottom)
  expect(saved.match(/\/AP <</g)?.length).toBe(3);

  // Free text: the panel's box writes the text as it is typed (after a pause, without leaving it).
  await page.getByRole('button', { name: 'Testo' }).click();
  await dragOnPage(page, [150, 340], [280, 380]);
  await page.keyboard.type('Primo');
  await page.keyboard.press('Escape');
  await expect(list).toContainText('Testo — Primo');
  await list.getByText('Testo — Primo').click();
  const box = page.getByTestId('annotation-panel').getByRole('textbox');
  await box.fill('Scritto mentre digito');
  await expect(box).toBeFocused();
  await expect(list).toContainText('Testo — Scritto mentre digito');
  await expect(box).toHaveValue('Scritto mentre digito');

  // Back to the pages.
  await page.getByRole('button', { name: 'Pagine' }).click();
  await expect(page.getByTestId('page')).toHaveCount(2);
});
