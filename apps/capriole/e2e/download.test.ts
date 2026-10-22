import { expect, test } from '@playwright/test';

test('«Scarica l’app per il computer» links the installers of the latest release', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.getByTestId('download-open').click();
  const dialog = page.getByTestId('download-dialog');
  await expect(dialog).toBeVisible();

  // Names without a version: the «latest» links never change from one release to the next.
  const latest = 'https://github.com/capriole-app/capriole/releases/latest/download/';
  const links = dialog.getByTestId('download-link');
  await expect(links).toHaveCount(3);
  for (const [i, file] of ['Capriole_x64-setup.exe', 'Capriole_amd64.deb', 'Capriole_amd64.AppImage'].entries()) {
    await expect(links.nth(i)).toHaveAttribute('href', latest + file);
  }
  await expect(dialog.getByRole('link', { name: 'Impronte SHA-256' })).toHaveAttribute('href', latest + 'SHA256SUMS');
  // The installer is not signed: the dialog says what Windows will show.
  await expect(dialog).toContainText('Esegui comunque');

  await page.keyboard.press('Escape');
  await expect(dialog).toBeHidden();

  // In English too.
  await page.goto('/en/');
  await page.getByRole('button', { name: 'Get the app for your computer' }).click();
  await expect(dialog).toContainText('Run anyway');
});
