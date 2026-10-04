// What the app shows of a certificate's names.

/** The common name (CN) of a distinguished name ("C=IT, O=…, CN=Capriole Test CA"); the whole when it has none. */
export function commonName(dn: string): string {
  const m = /(?:^|,\s*)CN=((?:\\,|[^,])+)/.exec(dn);
  return m ? m[1]!.replace(/\\,/g, ',').trim() : dn;
}
