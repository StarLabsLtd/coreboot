# CBFSTool tests

To run the tests do `pytest name_of_the_file.py`. E.g:

```shell
$ pytest elogtool_test.py
```

To run the extraction tests without pytest, use `python3 extraction_test.py`.
They use GNU `as`, `ld`, `objcopy`, and `readelf` with x86 support. Set
`CBFSTOOL_TEST_BIN_DIR` to test binaries built in a different directory.

## Dependencies

### Pytest

Requires `pytest`. To install it do:

```shell
$ pip install --user pytest
```

### Binaries

Make sure that you have compiled the cbfstool binaries before running the test. e.g:

```shell
$ cd $COREBOOT_SRC/util/cbfstool
$ make
```
