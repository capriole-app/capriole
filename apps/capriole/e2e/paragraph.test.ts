import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';
import { withoutFileAccess } from './downloads';

/**
 * One page, 400 × 400 points, 12 pt Helvetica, lines 14 points apart: a paragraph of three lines from
 * (30, 360) with free space below it (as shown, y ≈ 31–71); another from (30, 250) (y ≈ 141–181) with a
 * line of text close below it, at (30, 200) (y ≈ 191–203): no room there for a line more.
 */
function pdf(): Buffer {
  const content = [
    'BT /F1 12 Tf 14 TL 30 360 Td (Il primo paragrafo occupa) Tj T* (tre righe sulla pagina e) Tj T* (sotto ha spazio libero.) Tj ET',
    'BT /F1 12 Tf 14 TL 30 250 Td (Il secondo paragrafo ha) Tj T* (subito sotto un altro) Tj T* (testo che non si sposta.) Tj ET',
    'BT /F1 12 Tf 30 200 Td (Testo che sta sotto.) Tj ET',
  ].join('\n');
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    '<</Type/Page/Parent 2 0 R/MediaBox[0 0 400 400]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>',
    `<</Length ${content.length}>>\nstream\n${content}\nendstream`,
    '<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>',
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
  return { x: box.x + (x * box.width) / 400, y: box.y + (y * box.height) / 400 };
}

async function open(page: Page) {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'paragrafi.pdf', mimeType: 'application/pdf', buffer: pdf() });
  await page.getByTestId('page').first().dblclick();
  await page.getByRole('button', { name: 'Modifica testo' }).click();
  await page.getByRole('radio', { name: 'Paragrafo' }).click();
}

/**
 * Double-clicks the paragraph at (x, y) and waits for its box, with that text (again until it opens:
 * after an edit, the page's text is read again first).
 */
async function editParagraph(page: Page, x: number, y: number, text: string) {
  const p = await at(page, x, y);
  const box = page.getByTestId('paragraph-editor');
  await expect(async () => {
    if ((await box.count()) === 0) await page.mouse.dblclick(p.x, p.y);
    await expect(box).toHaveValue(text, { timeout: 500 });
  }).toPass();
}

const first = 'Il primo paragrafo occupa tre righe sulla pagina e sotto ha spazio libero.';
const second = 'Il secondo paragrafo ha subito sotto un altro testo che non si sposta.';

test.beforeEach(({ page }) => withoutFileAccess(page));

test('a paragraph written anew takes a line more in the free space below it; saved; undone', async ({ page }) => {
  await open(page);
  const box = page.getByTestId('paragraph-editor');
  await editParagraph(page, 60, 54, first);
  const longer =
    'Il primo paragrafo, adesso molto lungo, occupa quattro righe sulla pagina, dato che sotto ha spazio libero.';
  await box.fill(longer);
  await box.press('Control+Enter');
  await expect(box).toHaveCount(0);
  await expect(page.getByRole('alert')).toHaveCount(0);
  await expect(page.getByTestId('text-edit-notice')).toHaveCount(0); // its own font, its own size
  await editParagraph(page, 60, 54, longer);
  await box.press('Escape');

  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Salva' }).click();
  const saved = everything(readFileSync(await (await download).path())).toLowerCase();
  expect(saved).toContain(hex('adesso'));
  expect(saved).toContain('(testo che sta sotto.) tj'); // the rest of the page, byte for byte

  await page.getByRole('button', { name: 'Annulla' }).click();
  await editParagraph(page, 60, 54, first);
});

test('a text that does not fit: back to it, then written smaller', async ({ page }) => {
  await open(page);
  const box = page.getByTestId('paragraph-editor');
  await editParagraph(page, 60, 164, second);
  const longer = 'Il secondo paragrafo ha subito sotto un altro testo che non si sposta. Anche questo.';
  await box.fill(longer);
  await box.press('Control+Enter');
  const room = page.getByTestId('no-room');
  await expect(room).toContainText('servono 4 righe e sotto il paragrafo ce ne stanno 3');
  await expect(page.getByRole('alert')).toContainText('non ci sta');

  // Back to the text typed, unchanged; left again.
  await room.getByRole('button', { name: 'Torna al testo' }).click();
  await expect(box).toHaveValue(longer);
  await box.press('Control+Enter');
  await expect(room).toBeVisible();

  await room.getByRole('button', { name: 'Riduci il testo per farlo stare' }).click();
  await expect(room).toHaveCount(0);
  await expect(page.getByTestId('text-edit-notice')).toContainText(/Ridotto all?’?\s?\d+% per farlo stare/);
  await editParagraph(page, 60, 164, longer);
  await box.press('Escape');
  // The line below did not move.
  await editParagraph(page, 60, 197, 'Testo che sta sotto.');
});

test('Esc leaves the paragraph as it was, with no step of undo', async ({ page }) => {
  await open(page);
  const box = page.getByTestId('paragraph-editor');
  await editParagraph(page, 60, 54, first);
  await box.fill('Tutt’altro testo');
  await box.press('Escape');
  await expect(box).toHaveCount(0);
  await expect(page.getByRole('button', { name: 'Annulla' })).toBeDisabled();
  await editParagraph(page, 60, 54, first);
});
