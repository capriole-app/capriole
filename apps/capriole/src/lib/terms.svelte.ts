// Whether this person has accepted the terms of use (i18n/terms.ts) as they are now. Accepting is asked
// once, on the first screen, before a file can be opened (OpenCard): a box to tick, not only a link, so
// that the terms bind. Kept on this device with the version accepted: when the terms change,
// `termsVersion` changes and they are asked again.
import { termsKey, termsVersion } from './terms-version';

function read(): boolean {
  try {
    return localStorage.getItem(termsKey) === termsVersion;
  } catch {
    return false;
  }
}

let accepted = $state(read());

export const terms = {
  /** Accepted, in their present version (reactive). */
  get accepted(): boolean {
    return accepted;
  },
  accept(): void {
    accepted = true;
    try {
      localStorage.setItem(termsKey, termsVersion);
    } catch {
      // no storage: accepted until the page is closed, and asked again next time
    }
  },
};
