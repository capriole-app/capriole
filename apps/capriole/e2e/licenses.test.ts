import { expect, test } from '@playwright/test';

test('«Informazioni e licenze» lists Capriole’s licence and what it is built from', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.getByRole('button', { name: 'Informazioni e licenze' }).click();
  const dialog = page.getByTestId('licenses-dialog');
  await expect(dialog).toContainText('0BSD');

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
