#define _POSIX_C_SOURCE 200809L
#define main cdk2_checkpatch_filter_main
#include "/home/sean/Documents/.cdk2-worktrees/checkpatch-source-boundaries-after661/util/lint/cdk2-checkpatch-filter.c"
#undef main

int main(int argc, char **argv)
{
	struct buffer input = {0}, output = {0};
	const char *cursor;
	bool clean = false;
	size_t count = 0;

	if (argc != 3 || !read_file(argv[2], &input))
		return 2;
	cursor = input.data;
	while (cursor < input.data + input.length) {
		const char *separator = strstr(cursor, "\n\n");
		size_t length = separator == NULL ? strlen(cursor) : (size_t)(separator - cursor);
		char *record = strndup(cursor, length);

		if (record == NULL)
			return 2;
		printf("record=%zu length=%zu wire=%d\n", count++, length,
			uefi_information_tail(argv[1], record, length));
		free(record);
		if (separator == NULL)
			break;
		cursor = separator + 2;
	}
	printf("filter=%d ", filter_output(argv[1], &input, &output, &clean));
	printf("clean=%d\n", clean);
	if (output.data != NULL)
		fwrite(output.data, 1, output.length, stdout);
	free(input.data);
	free(output.data);
	return 0;
}
