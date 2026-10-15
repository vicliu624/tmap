"""Reject ABI-mismatched headers at link time; verify the installed profile."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main(compiler,source,build):
    build = Path(build)
    cache = (build/"CMakeCache.txt").read_text(encoding="utf-8")
    values = {}
    for key in ("TMAP_CACHE_PAGE_BYTES","TMAP_PACKAGE_CACHE_PAGES","TMAP_PACKAGE_SECTION_SLOTS"):
        values[key] = next(line.split("=",1)[1] for line in cache.splitlines() if line.startswith(key+":"))
    with tempfile.TemporaryDirectory(dir=build) as root:
        root = Path(root); cpp = root/"abi.cpp"
        cpp.write_text('#include "tmap/package.h"\nint main(){static tmap::PackageWorkspace w; tmap::OutdoorPackage p(w); return p.isOpen();}\n')
        base = [compiler,"-std=c++17","-I"+str(Path(source)/"include"),str(cpp),str(build/"libtmap.a"),"-o",str(root/"probe.exe")]
        good = subprocess.run(base+["-D"+key+"="+value for key,value in values.items()],capture_output=True,text=True)
        if good.returncode: raise ValueError(good.stderr)
        for key,replacement in (("TMAP_CACHE_PAGE_BYTES", "1024" if values["TMAP_CACHE_PAGE_BYTES"] != "1024" else "4096"),
                                ("TMAP_PACKAGE_CACHE_PAGES", "2" if values["TMAP_PACKAGE_CACHE_PAGES"] != "2" else "3"),
                                ("TMAP_PACKAGE_SECTION_SLOTS", "32" if values["TMAP_PACKAGE_SECTION_SLOTS"] != "32" else "37")):
            changed = dict(values)
            changed[key] = replacement
            bad = subprocess.run(base+["-D"+name+"="+value for name,value in changed.items()],capture_output=True,text=True)
            if not bad.returncode or "packageStorageAbi" not in bad.stderr: raise ValueError("ABI mismatch not rejected for "+key+": "+bad.stderr)


if __name__ == "__main__": main(*sys.argv[1:])
