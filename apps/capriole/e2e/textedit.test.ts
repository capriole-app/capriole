import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';
import { withoutFileAccess } from './downloads';

/**
 * One page, 300 × 400 points, in 24 pt Helvetica (no encoding: the standard one, which has no euro sign):
 * "Totale 120 euro" at (30, 300), i.e. y ≈ 78–106 as shown; "Scadenza marzo" at (30, 200), y ≈ 178–206;
 * and a free-text annotation whose appearance shows "Nota a margine" at y ≈ 310–340.
 */
function pdf(): Buffer {
  const content = 'BT /F1 24 Tf 30 300 Td (Totale 120 euro) Tj ET\nBT /F1 24 Tf 30 200 Td (Scadenza marzo) Tj ET';
  const note = 'BT /F1 14 Tf 4 10 Td (Nota a margine) Tj ET';
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    '<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 400]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>/Annots[6 0 R]>>',
    `<</Length ${content.length}>>\nstream\n${content}\nendstream`,
    '<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>',
    '<</Type/Annot/Subtype/FreeText/Rect[30 60 200 90]/Contents(Nota a margine)/DA(/F1 14 Tf 0 g)/AP<</N 7 0 R>>>>',
    `<</Type/XObject/Subtype/Form/BBox[0 0 170 30]/Resources<</Font<</F1 5 0 R>>>>/Length ${note.length}>>\nstream\n${note}\nendstream`,
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

/** Every stream of a file, decoded when it is Flate (else as it is), and the rest of the file. */
function everything(file: Buffer): string {
  const text = file.toString('latin1');
  let out = text;
  for (const m of text.matchAll(/stream\r?\n([\s\S]*?)\r?\nendstream/g)) {
    try {
      out += '\n' + inflateSync(Buffer.from(m[1]!, 'latin1')).toString('latin1');
    } catch {
      // not Flate
    }
  }
  return out;
}

const hex = (text: string) => Buffer.from(text, 'latin1').toString('hex');

async function at(page: Page, x: number, y: number): Promise<{ x: number; y: number }> {
  const box = (await page.getByTestId('page-surface').boundingBox())!;
  return { x: box.x + (x * box.width) / 300, y: box.y + (y * box.height) / 400 };
}

async function open(page: Page) {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'fattura.pdf', mimeType: 'application/pdf', buffer: pdf() });
  await page.getByTestId('page').first().dblclick();
  await page.getByRole('button', { name: 'Modifica testo' }).click();
}

/**
 * Double-clicks the line at (x, y) of the page and waits for its box, with that text (again until it opens:
 * after an edit, the page's text is read again first).
 */
async function editLine(page: Page, x: number, y: number, text: string) {
  const p = await at(page, x, y);
  const box = page.getByTestId('line-editor');
  await expect(async () => {
    if ((await box.count()) === 0) await page.mouse.dblclick(p.x, p.y);
    await expect(box).toHaveValue(text, { timeout: 500 });
  }).toPass();
}

test.beforeEach(({ page }) => withoutFileAccess(page));

test('writes a word of a line again in its own font, saves it, and undo brings the old one back', async ({ page }) => {
  await open(page);
  const p = await at(page, 60, 92);
  await page.mouse.move(p.x, p.y);
  await expect(page.getByTestId('line-hover')).toBeVisible();

  const box = page.getByTestId('line-editor');
  await editLine(page, 60, 92, 'Totale 120 euro');
  await box.fill('Totale 150 euro');
  await box.press('Enter');
  await expect(box).toHaveCount(0);
  // The same font could write it: nothing to say.
  await expect(page.getByTestId('text-edit-notice')).toHaveCount(0);
  await expect(page.getByRole('alert')).toHaveCount(0);

  // The page's text is read again: the line now says so.
  await editLine(page, 60, 92, 'Totale 150 euro');
  await box.press('Escape');

  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Salva' }).click();
  const saved = everything(readFileSync(await (await download).path())).toLowerCase();
  expect(saved).toContain(hex('Totale 150 euro'));
  expect(saved).not.toContain('totale 120 euro');
  expect(saved).toContain('(scadenza marzo) tj'); // the other line, byte for byte

  await page.getByRole('button', { name: 'Annulla' }).click();
  await editLine(page, 60, 92, 'Totale 120 euro');
});

test('Esc leaves the line as it was, with no step of undo', async ({ page }) => {
  await open(page);
  const box = page.getByTestId('line-editor');
  await editLine(page, 60, 192, 'Scadenza marzo');
  await box.fill('Scadenza aprile');
  await box.press('Escape');
  await expect(box).toHaveCount(0);
  await expect(page.getByRole('button', { name: 'Annulla' })).toBeDisabled();
  await editLine(page, 60, 192, 'Scadenza marzo');
});

test('says when a similar font wrote a character the old one lacks', async ({ page }) => {
  await open(page);
  const box = page.getByTestId('line-editor');
  await editLine(page, 60, 92, 'Totale 120 euro');
  await box.fill('Totale 120 €');
  await box.press('Enter');
  await expect(page.getByTestId('text-edit-notice')).toContainText('font simile (Helvetica)');
  // The sign drawn by that edit is on the line: edited again together with the text before it.
  await editLine(page, 60, 92, 'Totale 120 €');
  await box.fill('Totale 12$');
  await box.press('Enter');
  await expect(page.getByRole('alert')).toHaveCount(0);
  await editLine(page, 60, 92, 'Totale 12$');
});

test('an annotation’s text is not the page’s: refused, in Italian', async ({ page }) => {
  await open(page);
  const box = page.getByTestId('line-editor');
  await editLine(page, 60, 325, 'Nota a margine');
  await box.fill('Nota cambiata');
  await box.press('Enter');
  await expect(page.getByRole('alert')).toContainText('appartiene a un’annotazione');
  await expect(page.getByRole('button', { name: 'Annulla' })).toBeDisabled();
});
