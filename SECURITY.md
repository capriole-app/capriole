# Security policy

Capriole parses untrusted files: memory-safety bugs, hangs and resource exhaustion triggered by a crafted
PDF are security issues, and so is anything that makes a redaction leave data behind, a signature verify
when it should not, or a file leave the user's device.

## Reporting a vulnerability

Please report privately — do not open a public issue:

- through GitHub's private vulnerability reporting ("Security" tab → "Report a vulnerability"), or
- by e-mail to security@capriole.app.

Include the file that triggers the problem (minimized if you can) and what happens. You will get an
answer within a week; fixes are released with a regression test, and the reporter is credited unless
they ask otherwise.

## Supported versions

Capriole has not been released yet: only the `development` branch receives fixes.
