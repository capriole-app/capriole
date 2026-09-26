#!/usr/bin/env bash
# Regenerates the encrypted test fixtures with qpdf (an external tool: its output is test data, the
# library is never linked). Every fixture encrypts the same small document:
#   - one page whose content stream shows "(Hello encrypted) Tj";
#   - /Info /Title (Capriole test);
#   - an XMP /Metadata stream.
# Passwords: user "user", owner "owner", except the *_empty_user fixtures (user password empty).
set -euo pipefail
cd "$(dirname "$0")"

python3 - <<'PY'
parts = [b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n"]
offsets = {}
def obj(n, body):
    offsets[n] = sum(len(p) for p in parts)
    parts.append(b"%d 0 obj\n" % n + body + b"\nendobj\n")
content = b"BT /F1 12 Tf 72 720 Td (Hello encrypted) Tj ET"
xmp = b'<?xpacket begin="" id="W5M0MpCehiHzreSzNTczkc9d"?><x:xmpmeta xmlns:x="adobe:ns:meta/"></x:xmpmeta><?xpacket end="r"?>'
obj(1, b"<</Type/Catalog/Pages 2 0 R/Metadata 6 0 R>>")
obj(2, b"<</Type/Pages/Kids[3 0 R]/Count 1>>")
obj(3, b"<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]/Resources<</Font<</F1 5 0 R>>>>/Contents 4 0 R>>")
obj(4, b"<</Length %d>>\nstream\n" % len(content) + content + b"\nendstream")
obj(5, b"<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>")
obj(6, b"<</Type/Metadata/Subtype/XML/Length %d>>\nstream\n" % len(xmp) + xmp + b"\nendstream")
obj(7, b"<</Title(Capriole test)>>")
xref = sum(len(p) for p in parts)
parts.append(b"xref\n0 8\n0000000000 65535 f \n" + b"".join(b"%010d 00000 n \n" % offsets[i] for i in range(1, 8)))
parts.append(b"trailer\n<</Size 8/Root 1 0 R/Info 7 0 R/ID[<0123456789abcdef0123456789abcdef><0123456789abcdef0123456789abcdef>]>>\nstartxref\n%d\n%%%%EOF\n" % xref)
open("plain.pdf", "wb").write(b"".join(parts))
PY

weak=(--allow-weak-crypto)
qpdf "${weak[@]}" --encrypt user owner 40 -- plain.pdf r2_rc4_40.pdf
qpdf "${weak[@]}" --encrypt user owner 128 --use-aes=n -- plain.pdf r3_rc4_128.pdf
qpdf --encrypt user owner 128 --use-aes=y -- plain.pdf r4_aes_128.pdf
qpdf --encrypt "" owner 128 --use-aes=y -- plain.pdf r4_aes_128_empty_user.pdf
qpdf --encrypt user owner 128 --use-aes=y --cleartext-metadata -- plain.pdf r4_aes_128_cleartext_metadata.pdf
qpdf --encrypt user owner 256 --force-R5 -- plain.pdf r5_aes_256.pdf
qpdf --encrypt user owner 256 -- plain.pdf r6_aes_256.pdf
qpdf --encrypt "" owner 256 -- plain.pdf r6_aes_256_empty_user.pdf
qpdf --object-streams=generate --encrypt user owner 256 -- plain.pdf r6_aes_256_object_streams.pdf
qpdf --object-streams=generate --encrypt user owner 128 --use-aes=y -- plain.pdf r4_aes_128_object_streams.pdf
rm plain.pdf
