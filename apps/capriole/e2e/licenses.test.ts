import { expect, test } from '@playwright/test';

test('«Informazioni e licenze» lists Capriole’s licence and what it is built from', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.getByRole('button', { name: 'Informazioni e licenze' }).click();
  const dialog = page.getByTestId('licenses-dialog');
  await expect(dialog).toContainText('0BSD');
  await expect(dialog.getByTestId('about-versions')).toHaveText(/^versione \d+\.\d+\.\d+ · motore \d+\.\d+\.\d+$/);

  // The engine's dependencies, with what their licences ask to say.
  const engine = dialog.getByTestId('licenses-engine');
  await expect(engine).toContainText('Mbed TLS 3.6.7 — Apache-2.0');
  await expect(engine).toContainText('libjpeg-turbo');
  await engine.getByText(/^FreeType /).click();
  await expect(engine).toContainText('copyright © 2026 The FreeType Project');

  // The JavaScript packages that ship in the app (from its bundle), each with its licence.
  const npm = dialog.getByTestId('licenses-npm');
  await expect(npm).toContainText(/svelte \d+\.\d+\.\d+ — MIT/);
  await expect(npm).not.toContainText('@playwright/test'); // a tool of the build, not in the app

  // The desktop shell's crates only in the desktop app.
  await expect(dialog.getByTestId('licenses-crates')).toHaveCount(0);

  await dialog.getByRole('button', { name: 'Chiudi' }).click();
  await expect(dialog).toBeHidden();
});

test('the mark in the bar opens «Informazioni e licenze»', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.getByTestId('open-file').setInputFiles({
    name: 'vuoto.pdf',
    mimeType: 'application/pdf',
    buffer: Buffer.from(
      '%PDF-1.7\n1 0 obj\n<</Type/Catalog/Pages 2 0 R>>\nendobj\n2 0 obj\n<</Type/Pages/Kids[3 0 R]/Count 1>>\nendobj\n3 0 obj\n<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 200]>>\nendobj\ntrailer\n<</Root 1 0 R>>\n%%EOF\n',
      'latin1',
    ),
  });
  const mark = page.getByTestId('brand-mark');
  await expect(mark.locator('img').first()).toBeVisible();
  await mark.click();
  await expect(page.getByTestId('licenses-dialog')).toBeVisible();
});
