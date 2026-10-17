/* SPDX-License-Identifier: GPL-2.0-only */

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	unsigned char *der = NULL;
	X509 *certificate = NULL;
	BIO *input = NULL;
	int size, result = EXIT_FAILURE;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s trusted-certificate.pem\n", argv[0]);
		return EXIT_FAILURE;
	}
	input = BIO_new_file(argv[1], "r");
	if (input == NULL)
		goto out;
	certificate = PEM_read_bio_X509(input, NULL, NULL, NULL);
	if (certificate == NULL)
		goto out;
	size = i2d_X509(certificate, &der);
	if (size <= 0)
		goto out;

	puts("/* Generated from the configured public capsule certificate. */");
	puts("#include <cdk2/system_fmp_trust.h>");
	puts("const uint8_t cdk2_system_fmp_certificates[] = {");
	/* FMP's trust list contains big-endian lengths and padded DER certificates. */
	printf("\t0x%02x, 0x%02x, 0x%02x, 0x%02x,\n",
	       (unsigned int)size >> 24, ((unsigned int)size >> 16) & 0xff,
	       ((unsigned int)size >> 8) & 0xff, (unsigned int)size & 0xff);
	for (int i = 0; i < size; i++)
		printf("%s0x%02x,%s", i % 12 == 0 ? "\t" : " ", (unsigned int)der[i],
		       i % 12 == 11 ? "\n" : "");
	puts("");
	for (unsigned int i = 0; i < (4 - (unsigned int)size % 4) % 4; i++)
		puts("\t0x00,");
	puts("};");
	puts("const size_t cdk2_system_fmp_certificates_size =\n"
	     "\tsizeof(cdk2_system_fmp_certificates);");
	result = fflush(stdout) == EOF ? EXIT_FAILURE : EXIT_SUCCESS;
out:
	if (result != EXIT_SUCCESS)
		ERR_print_errors_fp(stderr);
	OPENSSL_free(der);
	X509_free(certificate);
	BIO_free(input);
	return result;
}
