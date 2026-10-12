import type { Page } from '@playwright/test';

/**
 * Chromium has the File System Access API, so the app saves through the system's dialogs there: for a
 * test of the download path (what Firefox and Safari do), the page runs without it.
 */
export async function withoutFileAccess(page: Page): Promise<void> {
  await page.addInitScript(() => {
    for (const name of ['showOpenFilePicker', 'showSaveFilePicker']) {
      Reflect.deleteProperty(Window.prototype, name);
      Reflect.deleteProperty(window, name);
    }
  });
}
