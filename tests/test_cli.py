import subprocess
import sys
from pathlib import Path


def main(executable,fixtures):
    package = str(Path(fixtures)/"outdoor.tmap")
    commands = [([],0),(["elevation","0.0005","0.0005"],0),(["measure-contours","0.001","0.001","16"],0),
                (["route-coordinates","0.00025","0","0.001","0.00075"],0),
                (["elevation","nan","0"],2),(["elevation","99999999","0"],2),
                (["measure-contours","0","0","256"],2),(["route-coordinates","bad","0","0","0"],2)]
    for args,expected in commands:
        result = subprocess.run([executable,package,*args],capture_output=True,text=True)
        if result.returncode != expected: raise ValueError((args,result.returncode,result.stdout,result.stderr))
    result = subprocess.run([executable,package,"route-coordinates","0.00025","0","0.001","0.00075"],capture_output=True,text=True,check=True)
    if "legs=2" not in result.stdout: raise ValueError(result.stdout)
    for filename,coordinates,expected in [
            ("administration.tmap",["0.001","0.001"],"Country / Province / West"),
            ("administration.tmap",["0.002","0.001"],"Country / Province"),
            ("legacy-administration.tmap",["0.001","0.001"],"Country / Province")]:
        result = subprocess.run([executable,str(Path(fixtures)/filename),"region",*coordinates],capture_output=True,text=True,check=True)
        if "region="+expected not in result.stdout: raise ValueError(result.stdout)


if __name__ == "__main__": main(*sys.argv[1:])
