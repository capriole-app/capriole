import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { withoutFileAccess } from './downloads';
import { invoice } from './invoice';

/** One page with a line of text in font /F1 (the font dictionary given). */
function pdf(font: string): Buffer {
  const content = 'BT /F1 24 Tf 30 300 Td (Archivio) Tj ET';
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    '<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 400]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>',
    `<</Length ${content.length}>>\nstream\n${content}\nendstream`,
    font,
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

const helvetica = '<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>';
const verdana = '<</Type/Font/Subtype/TrueType/BaseFont/Verdana/FirstChar 65/LastChar 65/Widths[684]>>';

async function open(page: Page, name: string, data: Buffer) {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.getByTestId('open-file').setInputFiles({ name, mimeType: 'application/pdf', buffer: data });
}

test.beforeEach(({ page }) => withoutFileAccess(page));

test('converts a document to PDF/A and saves it as a new file', async ({ page }) => {
  await open(page, 'archivio.pdf', pdf(helvetica));
  await page.getByRole('button', { name: 'PDF/A', exact: true }).click();
  const dialog = page.getByTestId('pdfa-dialog');
  await expect(dialog).toContainText("formato d'archivio");
  await dialog.getByRole('button', { name: 'Converti' }).click();
  await expect(dialog.getByRole('status')).toContainText('Il documento ora è PDF/A-2b.');
  await expect(dialog.getByTestId('pdfa-changes')).toContainText('Font incorporati');
  await expect(dialog.getByTestId('pdfa-changes')).toContainText('Profilo colore aggiunto');

  const download = page.waitForEvent('download');
  await dialog.getByRole('button', { name: 'Salva il PDF/A' }).click();
  const file = await download;
  expect(file.suggestedFilename()).toBe('archivio-pdfa.pdf');
  const saved = readFileSync(await file.path()).toString('latin1');
  expect(saved).toContain('<pdfaid:part>2</pdfaid:part>');
  expect(saved).toMatch(/\/S\s*\/GTS_PDFA1/);
  expect(saved).toMatch(/\/FontFile3/);
  await expect(dialog).toBeHidden();
  // The document declares it now; undo brings back the one before.
  await expect(page.getByRole('button', { name: 'PDF/A-2', exact: true })).toBeVisible();
  await page.getByRole('button', { name: 'Annulla' }).click();
  await expect(page.getByRole('button', { name: 'PDF/A', exact: true })).toBeVisible();
});

test('says why a document cannot be made PDF/A, and leaves it as it was', async ({ page }) => {
  await open(page, 'verdana.pdf', pdf(verdana));
  await page.getByRole('button', { name: 'PDF/A', exact: true }).click();
  const dialog = page.getByTestId('pdfa-dialog');
  await dialog.getByRole('button', { name: 'Converti' }).click();
  await expect(dialog.getByRole('alert')).toContainText('Il documento non può diventare PDF/A');
  await expect(dialog.getByTestId('pdfa-reasons')).toContainText('Il font «Verdana» non è incorporato');
  await expect(dialog.getByRole('button', { name: 'Converti' })).toHaveCount(0);
  await dialog.getByRole('button', { name: 'Annulla' }).click();
  await expect(page.getByRole('button', { name: 'Annulla' })).toBeDisabled();
});

test('makes a Factur-X invoice PDF/A-3 from its panel', async ({ page }) => {
  await open(page, 'fattura.pdf', pdf(helvetica));
  await page.getByRole('button', { name: /^Allegati/ }).click();
  await page
    .getByTestId('invoice-file')
    .setInputFiles({ name: 'fattura.xml', mimeType: 'text/xml', buffer: Buffer.from(invoice) });
  const card = page.getByTestId('invoice');
  await expect(card.getByRole('note')).toContainText('non è PDF/A-3');
  await card.getByRole('button', { name: 'Converti in PDF/A-3…' }).click();
  const dialog = page.getByTestId('pdfa-dialog');
  await expect(dialog).toContainText('Converti in PDF/A-3');
  await dialog.getByRole('button', { name: 'Converti' }).click();
  await expect(dialog.getByRole('status')).toContainText('PDF/A-3b');
  await expect(dialog.getByTestId('pdfa-changes')).toContainText('Allegati resi conformi a PDF/A-3');
  await dialog.getByRole('button', { name: 'Chiudi' }).click();
  await expect(card.getByRole('note')).toHaveCount(0);
});
