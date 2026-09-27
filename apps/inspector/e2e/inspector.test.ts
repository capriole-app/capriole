import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const repository = fileURLToPath(new URL('../../../', import.meta.url));

/** A one-page PDF with a Flate-free content stream and an /Info title. */
function samplePdf(): Buffer {
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    '<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]/Contents 4 0 R>>',
    '<</Length 24>>\nstream\nBT (Ciao capriole) Tj ET\nendstream',
    '<</Title(Prova)>>',
  ];
  let pdf = '%PDF-1.7\n';
  const offsets: number[] = [];
  bodies.forEach((body, i) => {
    offsets.push(pdf.length);
    pdf += `${i + 1} 0 obj\n${body}\nendobj\n`;
  });
  const xref = pdf.length;
  pdf += `xref\n0 ${bodies.length + 1}\n0000000000 65535 f \n`;
  pdf += offsets.map((o) => `${String(o).padStart(10, '0')} 00000 n \n`).join('');
  pdf += `trailer\n<</Size ${bodies.length + 1}/Root 1 0 R/Info 5 0 R>>\nstartxref\n${xref}\n%%EOF\n`;
  return Buffer.from(pdf, 'latin1');
}

async function openPdf(page: Page, name: string, buffer: Buffer) {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.locator('input[type=file]').setInputFiles({ name, mimeType: 'application/pdf', buffer });
}

test('opens a PDF and walks from the page to its content stream', async ({ page }) => {
  await openPdf(page, 'prova.pdf', samplePdf());
  await expect(page.locator('header').getByText('prova.pdf')).toBeVisible();
  await expect(page.getByText('Versione PDF')).toBeVisible();
  await expect(page.getByText('Tabelle dei riferimenti incrociati')).toBeVisible();

  await page.getByRole('tab', { name: 'Pagine' }).click();
  await page.getByRole('button', { name: /Pagina 1/ }).click();
  await expect(page.getByRole('heading', { name: 'Oggetto 3 0' })).toBeVisible();
  await expect(page.getByText('/Page', { exact: true })).toBeVisible();

  // The page drawn by the interpreter: an SVG image that the browser actually loads.
  const preview = page.getByRole('img', { name: 'Anteprima della pagina 1' });
  await expect(preview).toBeVisible();
  await expect.poll(() => preview.evaluate((img: HTMLImageElement) => img.complete && img.naturalWidth > 0)).toBe(true);
  await expect(page.getByText(/1 testi/)).toBeVisible();

  // The page's content stream, as operations.
  await expect(page.getByRole('heading', { name: 'Contenuto della pagina 1' })).toBeVisible();
  await expect(page.getByText('3 operazioni')).toBeVisible();
  const operations = page.locator('ol li');
  await expect(operations).toHaveCount(3);
  await expect(operations.nth(1)).toContainText('(Ciao capriole)');
  await expect(operations.nth(1)).toContainText('Tj');

  await page.getByRole('button', { name: '4 0 R' }).click();
  await expect(page.getByRole('heading', { name: 'Oggetto 4 0' })).toBeVisible();
  await expect(page.locator('pre')).toHaveText('BT (Ciao capriole) Tj ET');

  await page.getByRole('button', { name: 'Indietro' }).click();
  await expect(page.getByRole('heading', { name: 'Oggetto 3 0' })).toBeVisible();
});

test('saves the document, fully and incrementally', async ({ page }) => {
  const original = samplePdf();
  await openPdf(page, 'prova.pdf', original);
  await expect(page.getByText('Versione PDF')).toBeVisible();

  const full = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Salvataggio completo' }).click();
  const fullBytes = readFileSync(await (await full).path());
  expect(fullBytes.subarray(0, 8).toString('latin1')).toBe('%PDF-1.7');

  const incremental = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Incrementale' }).click();
  const download = await incremental;
  expect(download.suggestedFilename()).toBe('prova-incrementale.pdf');
  const bytes = readFileSync(await download.path());
  expect(bytes.subarray(0, original.length).equals(original)).toBe(true); // the original, byte for byte
  expect(bytes.subarray(original.length).toString('latin1')).toContain('/ModDate');
});

test('asks for the password of an encrypted file', async ({ page }) => {
  const encrypted = readFileSync(`${repository}tests/data/encrypted/r4_aes_128.pdf`);
  await openPdf(page, 'cifrato.pdf', encrypted);
  const dialog = page.getByRole('dialog');
  await expect(dialog.getByText('Documento protetto')).toBeVisible();

  await dialog.getByLabel('Password').fill('sbagliata');
  await dialog.getByRole('button', { name: 'Apri' }).click();
  await expect(dialog.getByRole('alert')).toHaveText('Password non valida.');

  await dialog.getByLabel('Password').fill('user');
  await dialog.getByRole('button', { name: 'Apri' }).click();
  await expect(dialog).toBeHidden();
  await expect(page.getByText(/Revisione 4: stringhe AES-128/)).toBeVisible();
});

test('lists the repairs of a damaged file', async ({ page }) => {
  const pdf = samplePdf().toString('latin1');
  const damaged = Buffer.from(pdf.slice(0, pdf.indexOf('xref')), 'latin1'); // no cross-reference table
  await openPdf(page, 'rotto.pdf', damaged);
  await expect(page.getByText(/Riparazioni applicate: \d+/)).toBeVisible();
  await expect(page.getByText('xref_reconstructed')).toBeVisible();
  await expect(page.getByRole('button', { name: 'Incrementale' })).toBeDisabled();
});

test('theme menu: light, dark, and following the system', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: 'Tema' }).click();
  await page.getByRole('menuitemradio', { name: 'Scuro' }).click();
  await expect(page.locator('html')).toHaveClass(/dark/);
  await page.getByRole('button', { name: 'Tema' }).click();
  await page.getByRole('menuitemradio', { name: 'Sistema' }).click();
  await page.getByRole('button', { name: 'Tema' }).click();
  await expect(page.getByRole('menuitemradio', { name: 'Sistema' })).toHaveAttribute('aria-checked', 'true');
});
