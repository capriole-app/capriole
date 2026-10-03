import { expect, test } from '@playwright/test';
import { withoutFileAccess } from './downloads';
import { readFileSync } from 'node:fs';

/** One page, 300 × 400 points, with a field of every kind that can be filled. */
function form(): Buffer {
  const w = '/Subtype/Widget/P 3 0 R/F 4';
  const stream = (data: string) =>
    `<</Type/XObject/Subtype/Form/BBox[0 0 14 14]/Length ${data.length}>>\nstream\n${data}\nendstream`;
  const bodies: Record<number, string> = {
    1: '<</Type/Catalog/Pages 2 0 R/AcroForm 4 0 R>>',
    2: '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    3: '<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 400]/Annots[10 0 R 11 0 R 12 0 R 13 0 R 14 0 R 15 0 R 16 0 R]>>',
    4: '<</Fields[10 0 R 11 0 R 12 0 R 17 0 R 15 0 R 16 0 R]/DR<</Font<</Helv 5 0 R>>>>/DA(/Helv 0 Tf 0 g)>>',
    5: '<</Type/Font/Subtype/Type1/BaseFont/Helvetica/Encoding/WinAnsiEncoding>>',
    10: `<</FT/Tx/T(nome)/TU(Nome)${w}/Rect[20 350 220 370]>>`,
    11: `<</FT/Tx/T(note)/TU(Note)/Ff 4096${w}/Rect[20 290 220 340]>>`,
    12: `<</FT/Btn/T(ok)/TU(Accetto)/V/Off/AS/Off/AP<</N<</Si 20 0 R/Off 21 0 R>>>>${w}/Rect[20 260 34 274]>>`,
    13: `<</Parent 17 0 R/AS/Off/AP<</N<</Rosso 20 0 R/Off 21 0 R>>>>${w}/Rect[20 230 34 244]>>`,
    14: `<</Parent 17 0 R/AS/Off/AP<</N<</Blu 20 0 R/Off 21 0 R>>>>${w}/Rect[60 230 74 244]>>`,
    15: `<</FT/Ch/Ff 131072/T(citta)/TU(Città)/Opt[(Roma)(Milano)]${w}/Rect[20 190 120 210]>>`,
    16: `<</FT/Ch/Ff 2097152/T(lingue)/TU(Lingue)/Opt[[(it)(Italiano)][(en)(English)][(de)(Deutsch)]]${w}/Rect[20 110 120 170]>>`,
    17: '<</FT/Btn/Ff 49152/T(colore)/TU(Colore)/Kids[13 0 R 14 0 R]>>',
    20: stream('0 g 2 2 10 10 re f'),
    21: stream(''),
  };
  const size = 22;
  let text = '%PDF-1.7\n';
  const offsets: Record<number, number> = {};
  for (const [n, body] of Object.entries(bodies)) {
    offsets[Number(n)] = text.length;
    text += `${n} 0 obj\n${body}\nendobj\n`;
  }
  const xref = text.length;
  text += `xref\n0 ${size}\n0000000000 65535 f \n`;
  for (let n = 1; n < size; n++) {
    text += n in offsets ? `${String(offsets[n]).padStart(10, '0')} 00000 n \n` : '0000000000 65535 f \n';
  }
  text += `trailer\n<</Size ${size}/Root 1 0 R>>\nstartxref\n${xref}\n%%EOF\n`;
  return Buffer.from(text, 'latin1');
}

// Saving here is a download (the path of browsers without file access).
test.beforeEach(({ page }) => withoutFileAccess(page));

test('fills a form on the page: text, check box, radio, combo, list; undoes; saves', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'modulo.pdf', mimeType: 'application/pdf', buffer: form() });
  await expect(page.getByTestId('page')).toHaveCount(1);

  // A page with a form opens ready to fill it.
  await page.getByTestId('page').first().dblclick();
  await expect(page.getByRole('button', { name: 'Compila il modulo' })).toHaveAttribute('aria-pressed', 'true');
  const layer = page.getByTestId('form-layer');

  const name = layer.getByRole('textbox', { name: 'Nome' });
  await name.fill('Ada Lovelace');
  // Tab to the next field and type at once: it keeps the focus while the name is written.
  await name.press('Tab');
  await page.keyboard.type('Prima riga\nseconda riga');
  await expect(name).toHaveValue('Ada Lovelace');
  // The panel lists the page's fields with their values (and not their widgets as annotations).
  await expect(page.getByTestId('field-list')).toContainText('Ada Lovelace');
  await expect(page.getByTestId('annotation-list')).not.toContainText('Campo modulo');
  const notes = layer.getByRole('textbox', { name: 'Note' });
  await expect(notes).toBeFocused();
  await notes.blur();
  await expect(notes).toHaveValue('Prima riga\nseconda riga');

  await layer.getByRole('checkbox', { name: 'Accetto' }).check();
  await expect(layer.getByRole('checkbox', { name: 'Accetto' })).toBeChecked();
  await layer.getByRole('radio', { name: 'Colore: Blu' }).check();
  await expect(layer.getByRole('radio', { name: 'Colore: Blu' })).toBeChecked();
  await layer.getByRole('combobox', { name: 'Città' }).selectOption('Milano');
  await expect(layer.getByRole('combobox', { name: 'Città' })).toHaveValue('Milano');
  await layer.getByRole('listbox', { name: 'Lingue' }).selectOption(['it', 'de']);
  await expect(layer.getByRole('listbox', { name: 'Lingue' })).toHaveValues(['it', 'de']);

  // Undo takes back the last value only.
  await page.getByRole('button', { name: 'Annulla' }).click();
  await expect(layer.getByRole('listbox', { name: 'Lingue' })).toHaveValues([]);
  await expect(layer.getByRole('combobox', { name: 'Città' })).toHaveValue('Milano');
  await layer.getByRole('listbox', { name: 'Lingue' }).selectOption(['en']);
  await expect(layer.getByRole('listbox', { name: 'Lingue' })).toHaveValues(['en']);

  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Salva' }).click();
  const saved = readFileSync(await (await download).path()).toString('latin1');
  expect(saved).toMatch(/\/V \(Ada Lovelace\)/);
  // A text string: PDFDocEncoding, or UTF-16BE (as here, with its line break).
  const utf16 = '\u00fe\u00ff' + [...'Prima riga\nseconda riga'].map((c) => `\u0000${c}`).join('');
  expect(saved.includes(`/V (${utf16})`) || saved.includes('/V (Prima riga')).toBe(true);
  expect(saved).toMatch(/\/V \/Si/);
  expect(saved).toMatch(/\/V \/Blu/);
  expect(saved).toMatch(/\/V \(Milano\)/);
  expect(saved).toMatch(/\/V \(en\)/);
  expect(saved).toMatch(/\/AS \/Si/);
  // The appearances of the values, made by the engine (text, multiline, combo, list).
  expect(saved.match(/\/Tx BMC/g)?.length).toBeGreaterThanOrEqual(4);
});
