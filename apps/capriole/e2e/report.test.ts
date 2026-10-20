import { expect, test, type Page } from '@playwright/test';

// The service is never reached: the report is caught on its way (also when the suite runs against the
// app as served, deploy/web/test.sh, or capriole.app itself).
async function catchReports(page: Page, status = 201): Promise<string[]> {
  const bodies: string[] = [];
  await page.route('**/api/report', async (route) => {
    bodies.push(route.request().postData() ?? '');
    await route.fulfill({ status, contentType: 'application/json', body: '{}' });
  });
  return bodies;
}

const pdf = (name: string) => ({
  name,
  mimeType: 'application/pdf',
  buffer: Buffer.from(
    '%PDF-1.7\n1 0 obj\n<</Type/Catalog/Pages 2 0 R>>\nendobj\n2 0 obj\n<</Type/Pages/Kids[3 0 R]/Count 1>>\nendobj\n3 0 obj\n<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 200]>>\nendobj\ntrailer\n<</Root 1 0 R>>\n%%EOF\n',
    'latin1',
  ),
});

test('a report shows all it sends, and sends nothing of the document but its traits', async ({ page }) => {
  const bodies = await catchReports(page);
  await page.goto('/');
  await page.getByTestId('open-file').setInputFiles(pdf('contratto-mario-rossi.pdf'));
  await expect(page.getByTestId('brand-mark')).toBeVisible();

  await page.getByRole('button', { name: 'Altro' }).click();
  await page.getByRole('menuitem', { name: 'Segnala un problema' }).click();
  const dialog = page.getByTestId('report-dialog');
  const sent = dialog.getByTestId('report-sent');
  await expect(sent).toContainText(/\d+\.\d+\.\d+/);
  await expect(sent).toContainText('PDF 1.7, 1 pagina, non cifrato, <1MB');
  await expect(sent).toContainText('Chrome');

  // Something must be said.
  await dialog.getByRole('button', { name: 'Invia' }).click();
  await expect(dialog.getByRole('alert')).toHaveText('Scrivi che cosa è successo.');
  expect(bodies).toHaveLength(0);

  await dialog.getByRole('textbox').fill('La miniatura resta bianca.');
  await dialog.getByRole('button', { name: 'Invia' }).click();
  await expect(dialog.getByRole('status')).toHaveText('Grazie! La segnalazione è arrivata.');
  expect(bodies).toHaveLength(1);
  const report = JSON.parse(bodies[0]);
  expect(report).toMatchObject({
    v: 1,
    description: 'La miniatura resta bianca.',
    platform: 'web',
    language: 'it',
    error: null,
    document: { pdfVersion: '1.7', pages: 1, encrypted: false, size: '<1MB' },
  });
  expect(bodies[0]).not.toContain('mario');
  expect(bodies[0]).not.toContain('Mozilla'); // the browser in short, not the whole user agent
  await dialog.getByRole('button', { name: 'Chiudi' }).click();
  await expect(dialog).toBeHidden();
});

test('an error can be reported from where it is said, and a refusal of the service is said too', async ({ page }) => {
  const bodies = await catchReports(page, 429);
  await page.goto('/');
  await page.getByTestId('open-file').setInputFiles({ ...pdf('rotto.pdf'), buffer: Buffer.from('not a pdf at all') });
  const error = page.getByRole('alert');
  await expect(error).toBeVisible();
  await error.getByRole('button', { name: 'Segnala' }).click();

  const dialog = page.getByTestId('report-dialog');
  const message = (await error.locator('span').textContent())?.trim() ?? '';
  await expect(dialog.getByTestId('report-sent')).toContainText(message);
  await expect(dialog.getByTestId('report-sent')).toContainText('<1MB');
  // With an error, the text may be left empty.
  await dialog.getByRole('button', { name: 'Invia' }).click();
  await expect(dialog.getByRole('alert')).toHaveText('Troppe segnalazioni in poco tempo: riprova fra un’ora.');
  expect(JSON.parse(bodies[0])).toMatchObject({ description: '', error: message, document: { pdfVersion: null } });
  expect(bodies[0]).not.toContain('rotto');
});
