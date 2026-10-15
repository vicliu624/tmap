# Host PBF codec bounds

Encoded headers, encoded blobs and raw blobs are limited separately to 64 KiB,
64 MiB and 32 MiB. These are host producer inputs, not MCU buffers or a whole
Python process RSS guarantee. Parsed entities and object overhead remain extra.

- Raw/zlib/bzip2 output remains checked against raw_size, with bounded standard
  decoder output and end/trailing validation.
- LZMA additionally uses a 64 MiB decoder-memory ceiling. A one-byte ALONE
  stream declaring a 1 GiB dictionary rejects without allocating that dictionary.
- Optional LZ4 always gets an explicit positive capacity. Passing zero would
  enable its embedded-size convention; a declared-empty PBF instead uses capacity
  one and still requires exactly zero output. Raw block format is used.
- Optional ZSTD first checks declared frame content size and caps the window at
  32 MiB, then uses a window-limited stream decoder with at most raw_size+1
  returned bytes. A separate frame/block-length walk rejects truncated or extra
  data. This is needed because zstandard 0.25.0 one-shot empty-frame decoding
  accepted trailing bytes despite allow_extra_data=False in the observed test.

PBF tests pass **11/11 without skips** with lz4 4.4.5 and zstandard 0.25.0.
Coverage includes empty/nonempty, known/unknown ZSTD sizes, checksum enabled/
disabled, too-small raw_size, truncation, trailing data, concatenated frames,
128 MiB declared ZSTD window and oversized LZMA dictionary. Default standard-
library-only runs explicitly skip the two optional-codec tests. CI installs the
tested optional packages; this configuration is not a claim of remote CI success.

API contracts:
[Python LZMA memory limit](https://docs.python.org/3/library/lzma.html#lzma.LZMADecompressor),
[python-lz4 block capacity](https://python-lz4.readthedocs.io/en/stable/lz4.block.html),
[python-zstandard decompression](https://python-zstandard.readthedocs.io/en/latest/decompressor.html).

The current MCU library, object sizes and resource reports are unaffected.
Installation copies these Python scripts for Center; Python and optional codecs
remain explicit host dependencies.
