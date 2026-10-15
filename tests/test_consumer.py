from pathlib import Path
import subprocess
import sys


def main(cmake, source, build, compiler):
    build = Path(build)
    prefix = build/"install"
    consumer = build/"consumer"
    subprocess.run([cmake, "--install", str(build), "--prefix", str(prefix)], check=True)
    cache = (build / 'CMakeCache.txt').read_text()
    if 'TMAP_INSTALL_TOOLS:BOOL=ON' in cache:
        installed_tools = prefix / 'share/tmap/tools'
        if not (installed_tools / 'requirements-geometry.txt').is_file():
            raise AssertionError('installed optional geometry dependency declaration missing')
        for name in ('import_osm.py', 'tmap_pack.py', 'merge_osm_regions.py'):
            subprocess.run([sys.executable, str(installed_tools / name), '--help'],
                           cwd=prefix, capture_output=True, text=True, check=True)
    subprocess.run([cmake, "-S", str(Path(source)/"tests/consumer"), "-B", str(consumer), "-G", "Ninja",
                    f"-DCMAKE_PREFIX_PATH={prefix}", f"-DCMAKE_CXX_COMPILER={compiler}"], check=True)
    subprocess.run([cmake, "--build", str(consumer)], check=True)
    executable = consumer/("consumer.exe" if sys.platform == "win32" else "consumer")
    subprocess.run([str(executable)], check=True)
    child = build/"consumer-subdir"
    subprocess.run([cmake,"-S",str(Path(source)/"tests/consumer-subdir"),"-B",str(child),"-G","Ninja",
                    f"-DTMAP_SOURCE={source}",f"-DCMAKE_CXX_COMPILER={compiler}"],check=True)
    subprocess.run([cmake,"--build",str(child)],check=True)
    executable = child/("parent_consumer.exe" if sys.platform == "win32" else "parent_consumer")
    subprocess.run([str(executable)],check=True)


if __name__ == "__main__": main(*sys.argv[1:])
