// Who offers Capriole, and which terms of use are in force: a plain module, so that the end-to-end tests
// (playwright.config.ts) can mark them accepted. The text is in i18n/terms.ts; accepting, in terms.svelte.ts.

/** Who offers Capriole, named in the terms. */
export const provider = {
  name: 'Bobolar Ltd',
  contact: 'hello@capriole.app',
};

/** The terms' date: change it with their text, and everyone is asked to accept them again. */
export const termsVersion = '2026-10-14';

/** Where this device keeps the version accepted (localStorage). */
export const termsKey = 'capriole.terms';
