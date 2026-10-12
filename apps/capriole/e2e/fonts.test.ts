import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';
import { withoutFileAccess } from './downloads';

// The fonts of the engine's tests (tests/data/fonts/make_fixtures.py): CprSquare, a font of squares and
// bars — subset.ttf has A, full.ttf A, B and C, restricted.ttf forbids embedding, square.cff is CFF.
const fonts = fileURLToPath(new URL('../../../tests/data/fonts/', import.meta.url));
const font = (name: string) => readFileSync(fonts + name);

/**
 * One page, 400 × 400 points: "AA" in 20 pt CprSquare at (30, 360), a composite font (Identity-H) whose
 * embedded subset has only A (CID 1 = glyph 1).
 */
function pdf(): Buffer {
  const content = 'BT /F1 20 Tf 30 360 Td <00010001> Tj ET';
  const program = font('subset.ttf');
  const toUnicode =
    '/CIDInit /ProcSet findresource begin 12 dict begin begincmap /CMapName /CprSquare def 1 begincodespacerange ' +
    '<0000> <FFFF> endcodespacerange 1 beginbfchar <0001> <0041> endbfchar endcmap CMapName currentdict /CMap ' +
    'defineresource pop end end';
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    '<</Type/Page/Parent 2 0 R/MediaBox[0 0 400 400]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>',
    `<</Length ${content.length}>>\nstream\n${content}\nendstream`,
    '<</Type/Font/Subtype/Type0/BaseFont/ABCDEF+CprSquare/Encoding/Identity-H/DescendantFonts[6 0 R]/ToUnicode 9 0 R>>',
    '<</Type/Font/Subtype/CIDFontType2/BaseFont/ABCDEF+CprSquare/CIDSystemInfo<</Registry(Adobe)/Ordering(Identity)/Supplement 0>>' +
      '/FontDescriptor 7 0 R/CIDToGIDMap/Identity/DW 1000/W[1[700]]>>',
    '<</Type/FontDescriptor/FontName/ABCDEF+CprSquare/Flags 32/FontBBox[0 0 900 1400]/ItalicAngle 0/Ascent 800' +
      '/Descent -200/CapHeight 700/StemV 80/FontFile2 8 0 R>>',
    `<</Length ${program.length}/Length1 ${program.length}>>\nstream\n${program.toString('latin1')}\nendstream`,
    `<</Length ${toUnicode.length}>>\nstream\n${toUnicode}\nendstream`,
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

async function open(page: Page) {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'quadrati.pdf', mimeType: 'application/pdf', buffer: pdf() });
  await page.getByTestId('page').first().dblclick();
  await page.getByRole('button', { name: 'Modifica testo' }).click();
  await page.getByRole('radio', { name: 'Paragrafo' }).click();
}

/** Writes the paragraph "AA" (at the page's top left) anew as "AB". */
async function writeAB(page: Page) {
  const surface = (await page.getByTestId('page-surface').boundingBox())!;
  const box = page.getByTestId('paragraph-editor');
  await expect(async () => {
    if ((await box.count()) === 0)
      await page.mouse.dblclick(surface.x + (40 * surface.width) / 400, surface.y + (33 * surface.height) / 400);
    await expect(box).toHaveValue('AA', { timeout: 500 });
  }).toPass();
  await box.fill('AB');
  await box.press('Control+Enter');
  await expect(box).toHaveCount(0);
}

test.beforeEach(({ page }) => withoutFileAccess(page));

test('a letter the subset lacks: the font to give, then its glyph added from it; kept on the device', async ({
  page,
}) => {
  await open(page);
  const section = page.getByTestId('fonts-section');
  await expect(section).toBeVisible();
  await expect(page.getByTestId('font-list')).toHaveCount(0);

  // No font given: B goes to a similar font, and the app says which font would have written it.
  await writeAB(page);
  await expect(page.getByTestId('text-edit-notice')).toContainText('font simile');
  const missing = page.getByTestId('missing-fonts');
  await expect(missing).toContainText('aggiungi il font CprSquare');

  // A file that is not a TrueType font is refused, in Italian; one whose licence forbids it is listed so.
  await page.getByTestId('font-input').setInputFiles([
    { name: 'square.cff', mimeType: 'font/otf', buffer: font('square.cff') },
    { name: 'restricted.ttf', mimeType: 'font/ttf', buffer: font('restricted.ttf') },
  ]);
  await expect(section.getByRole('alert')).toContainText('square.cff: non è un font TrueType');
  await expect(page.getByTestId('font-list')).toContainText('la licenza non consente di aggiungerne lettere');
  await page.getByRole('button', { name: 'Rimuovi restricted.ttf' }).click();
  await expect(page.getByTestId('font-list')).toHaveCount(0);

  // The full font, from the hint's button.
  const chooser = page.waitForEvent('filechooser');
  await missing.getByRole('button', { name: 'Aggiungi font…' }).click();
  await (await chooser).setFiles({ name: 'full.ttf', mimeType: 'font/ttf', buffer: font('full.ttf') });
  await expect(page.getByTestId('font-list')).toContainText('full.ttf');
  await expect(section.getByRole('alert')).toHaveCount(0);

  // The edit again: B in the document's own font now, its glyph added to the subset.
  await page.getByRole('button', { name: 'Annulla' }).click();
  await writeAB(page);
  await expect(page.getByTestId('text-edit-notice')).toContainText(
    'Aggiunta 1 lettera al font incorporato (CprSquare) dal font che hai fornito.',
  );
  await expect(page.getByTestId('missing-fonts')).toHaveCount(0);
  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Salva' }).click();
  const saved = everything(readFileSync(await (await download).path())).toLowerCase();
  expect(saved).toContain('<0002> <0042>'); // the subset's /ToUnicode says CID 2 is B
  expect(saved).not.toMatch(/\/basefont\s*\/helvetica/); // no similar font

  // Remembered on the device: there after a reload; removed, gone after another.
  await page.reload();
  await open(page);
  await expect(page.getByTestId('font-list')).toContainText('full.ttf');
  await page.getByRole('button', { name: 'Rimuovi full.ttf' }).click();
  await expect(page.getByTestId('font-list')).toHaveCount(0);
  await page.reload();
  await open(page);
  await expect(page.getByTestId('fonts-section')).toBeVisible();
  await expect(page.getByTestId('font-list')).toHaveCount(0);
});
