# Source provenance

The unchanged v1 Reader, search implementation and administrative-path helper
were copied from Trail Mate's working tree on 2026-10-14 (HEAD
`f38321dbe321fa75ad8089bedd3133726d386997`). They remain under the repository's
GNU Affero General Public License version 3; LICENSE and COPYRIGHT are preserved.
Working-tree file identities, rather than HEAD alone, establish their origin:

- src/reader.cpp: SHA256 6437D3A926C500EEAEA87AC8BF88A023A9CBA7EF8DB589149B08381118319DB7
- src/search.cpp: SHA256 B572E54105EE69CA9FEC4382FB698DED2AC8C01147510A6EA1AC2305A9573582
- include/tmap/reader.h: SHA256 52F4C287C7E1C413C64B04758BBFFAD1E5514755EBFF62D916FA2DF39E2E9E48

The new outdoor profile is independently versioned. It does not change Trail
Mate, Center, their map files, or the v1 reader's public contract.

Subsequent extension: reader.h adds a friend declaration for the new bounded
LegacyAdministrativeQuery adapter. Reader layout/existing methods and
reader.cpp/search.cpp/administrative_path.h remain unchanged. The header hash
above identifies the copied original, not the extended current header. New
administration sources do not replace the original queryBounds implementation.
