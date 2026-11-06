"""Sage interface to the standalone plethy executable.

Load this file in Sage, then call ``plethysm(s[2,1], s[1,1])``. The binary
is selected from ``$PLETHY_BIN``, ``PATH``, or the sibling ``../plethy``.
"""

import functools
import os
import shutil
import subprocess

from sage.combinat.partition import Partition
from sage.combinat.sf.sf import SymmetricFunctions
from sage.rings.rational_field import QQ

_ROOT = os.path.dirname(os.path.abspath(__file__))


def _binary():
    return (os.environ.get("PLETHY_BIN") or shutil.which("plethy")
            or os.path.join(_ROOT, "..", "plethy"))


def _parts(value):
    if hasattr(value, "monomial_coefficients"):
        terms = _schur()(value).monomial_coefficients()
        if len(terms) != 1:
            raise ValueError("expected a single Schur basis element")
        (part, coefficient), = terms.items()
        if coefficient != 1:
            raise ValueError("expected a Schur basis element with coefficient 1")
        value = part
    return tuple(int(x) for x in value)


@functools.cache
def _schur():
    return SymmetricFunctions(QQ).schur()


def _run(outer, inner, max_length=None, max_part=None, hash_only=False):
    args = [_binary(), "--raw"]
    if hash_only:
        args.append("-qH")
    if max_length is not None:
        args.extend(("-l", str(int(max_length))))
    if max_part is not None:
        args.extend(("-p", str(int(max_part))))
    args.extend((",".join(map(str, outer)), ",".join(map(str, inner))))
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


def plethysm(outer, inner, *, max_length=None, max_part=None):
    """Compute ``s_outer[s_inner]`` and return a Schur-basis element."""
    output = _run(_parts(outer), _parts(inner), max_length, max_part)
    terms = []
    for line in output.splitlines():
        fields = [int(x) for x in line.split()]
        coefficient, length = fields[:2]
        terms.append((Partition(fields[2:2 + length]), coefficient))
    return _schur().sum_of_terms(terms, distinct=True)


def plethysm_hash128(outer, inner):
    """Return the 128-bit canonical hash of ``s_outer[s_inner]``."""
    return int(_run(_parts(outer), _parts(inner), hash_only=True).strip(), 16)
