# Source provenance

The display Reader, search implementation and administrative-path helper
were copied from Trail Mate's working tree on 2026-10-14 (HEAD
`f38321dbe321fa75ad8089bedd3133726d386997`). They remain under the repository's
GNU Affero General Public License version 3; LICENSE and COPYRIGHT are preserved.
Working-tree file identities, rather than HEAD alone, establish their origin:

- src/reader.cpp: SHA256 6437D3A926C500EEAEA87AC8BF88A023A9CBA7EF8DB589149B08381118319DB7
- src/search.cpp: SHA256 B572E54105EE69CA9FEC4382FB698DED2AC8C01147510A6EA1AC2305A9573582
- include/tmap/reader.h: SHA256 52F4C287C7E1C413C64B04758BBFFAD1E5514755EBFF62D916FA2DF39E2E9E48

The hashes above identify the original imported files, not every later SDK
extension. The outdoor and persistent-route modules extend the independent
library without modifying Trail Mate firmware.

Subsequent extension: reader.h adds a friend declaration for the new bounded
LegacyAdministrativeQuery adapter. Reader layout/existing methods and
search.cpp/administrative_path.h remain unchanged. The header hash above
identifies the copied original, not the extended current header. New
administration sources do not replace the original queryBounds implementation.

Native-container extension: reader.cpp adds a header-dispatch branch and
reader.h declares the private openUnified method. The parser itself lives in
src/unified_reader.cpp. Reader's existing public signatures and workspace/layout
are retained. Consequently the original reader.cpp hash above is provenance
for the import checkpoint, not a claim that the current extended file is
byte-identical. Existing display/search behavior is covered by the recorded
baseline comparisons and native-container regression tests.
