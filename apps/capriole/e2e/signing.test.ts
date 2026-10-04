import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { withoutFileAccess } from './downloads';

// The test PKI (tests/data/signing/make_fixtures.sh): throwaway keys, password "capriole".
const signing = fileURLToPath(new URL('../../../tests/data/signing/', import.meta.url));

/** One page, 300 × 400 points, with a line of text. */
function pdf(): Buffer {
  const content = 'BT /F1 24 Tf 30 300 Td (Contratto) Tj ET';
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    '<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 400]/Contents 4 0 R/Resources<</Font<</F1<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>>>>>>>',
    `<</Length ${content.length}>>\nstream\n${content}\nendstream`,
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

async function chooseKey(page: Page, password: string) {
  await page.getByTestId('sign-key').setInputFiles(signing + 'ec.p12');
  const field = page.getByLabel('Password del certificato');
  await field.fill(password);
  await field.press('Tab');
}

/** Signs in the dialog (already open) and gives the signed file that was downloaded. */
async function signAndSave(page: Page): Promise<{ name: string; data: Buffer }> {
  await chooseKey(page, 'capriole');
  await expect(page.getByTestId('sign-certificate')).toContainText('Anna Bianchi (test)');
  await page.getByLabel('Motivo').fill('Approvazione');
  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Firma e salva' }).click();
  const file = await download;
  return { name: file.suggestedFilename(), data: readFileSync(await file.path()) };
}

test.beforeEach(({ page }) => withoutFileAccess(page));

test('signs a document invisibly, then visibly, and both signatures verify', async ({ page }) => {
  const original = pdf();
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'contratto.pdf', mimeType: 'application/pdf', buffer: original });

  // Not signed yet; the dialog tells a wrong password.
  await page.getByRole('button', { name: /^Firme/ }).click();
  const panel = page.getByTestId('signatures-panel');
  await expect(panel).toContainText('Il documento non è firmato.');
  await panel.getByRole('button', { name: 'Firma il documento…' }).click();
  await expect(page.getByRole('dialog')).toContainText('Firma invisibile');
  await chooseKey(page, 'sbagliata');
  await expect(page.getByRole('alert')).toContainText('Password errata.');
  await expect(page.getByRole('button', { name: 'Firma e salva' })).toBeDisabled();

  // Signed and saved: a new revision of the file — the original bytes come first.
  const first = await signAndSave(page);
  expect(first.name).toBe('contratto-firmato.pdf');
  expect(first.data.subarray(0, original.length).equals(original)).toBe(true);
  expect(first.data.toString('latin1')).toMatch(/\/SubFilter\s*\/ETSI\.CAdES\.detached/);
  await expect(page.getByRole('dialog')).toBeHidden();
  await expect(panel.getByTestId('signature-list')).toContainText('Firma valida');
  await expect(panel.getByTestId('signature-list')).toContainText('Anna Bianchi (test)');
  await expect(panel.getByTestId('signature-list')).toContainText('Motivo: Approvazione');

  // A visible signature drawn on the page: the first stays valid, made before a later revision.
  await page.getByTestId('page').first().dblclick();
  await page.getByRole('button', { name: 'Firma (disegna il riquadro)' }).click();
  const box = (await page.getByTestId('page-surface').boundingBox())!;
  await page.mouse.move(box.x + box.width * 0.1, box.y + box.height * 0.8);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width * 0.8, box.y + box.height * 0.95, { steps: 5 });
  await page.mouse.up();
  await expect(page.getByRole('dialog')).toContainText('Firma visibile nel riquadro disegnato a pagina 1');
  const second = await signAndSave(page);
  expect(second.data.subarray(0, first.data.length).equals(first.data)).toBe(true);
  expect(second.data.toString('latin1').match(/\/ByteRange/g)).toHaveLength(2);

  // A note added afterwards and saved: appended after the signatures, which stay valid.
  await page.getByRole('button', { name: 'Nota' }).click();
  await page.mouse.click(box.x + box.width * 0.5, box.y + box.height * 0.3);
  await expect(page.getByTestId('annotation-list')).toContainText('Nota');
  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Salva' }).click();
  const edited = readFileSync(await (await download).path());
  expect(edited.subarray(0, second.data.length).equals(second.data)).toBe(true);

  // Read back: two signatures, valid; both made before the note.
  await page.getByRole('button', { name: 'Pagine', exact: true }).click();
  await page.getByRole('button', { name: 'Chiudi il documento' }).click();
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'contratto-firmato.pdf', mimeType: 'application/pdf', buffer: edited });
  await page.getByRole('button', { name: /^Firme/ }).click();
  const list = page.getByTestId('signature-list').getByRole('listitem');
  await expect(list).toHaveCount(2);
  await expect(list.nth(0)).toContainText('Firma valida · documento modificato dopo la firma');
  await expect(list.nth(1)).toContainText('Firma valida · documento modificato dopo la firma');
  await expect(list.nth(1)).toContainText('Vai alla pagina 1');
});
