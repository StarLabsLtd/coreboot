These public Secure Boot assets belong to the fixed-key SMM owner. Each
X.509 entry in PK, KEK and db has its own EFI signature list. The firmware
image supplies these three lists. The authenticated dbx baseline, append
list and signed dbx update seed and maintain the persistent revocation
database. Their order and signature-owner GUIDs match the payload factory
defaults in UefiPayloadPkg/EnrollDefaultKeys and edIIe.
