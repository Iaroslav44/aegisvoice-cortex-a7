"""Exercise the real WAV CLI, including accidental source-file overwrite paths."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import wave

exe = str(pathlib.Path(sys.argv[1]).resolve())


def run(*args, ok=True):
    result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True)
    if (result.returncode == 0) != ok:
        raise AssertionError(result.stdout + result.stderr)


with tempfile.TemporaryDirectory(prefix="aegisvoice-test-") as directory:
    root = pathlib.Path(directory)
    source, target = root / "input.wav", root / "output.wav"
    with wave.open(str(source), "wb") as wav:
        wav.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
        wav.writeframes(struct.pack("<645h", *([1200] * 645)))
    original = source.read_bytes()
    run(source, target)
    with wave.open(str(target), "rb") as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()) == (1, 2, 16000, 645)
        assert struct.unpack("<h", wav.readframes(1))[0] == 1200
    saved = target.read_bytes()
    run(source, target, ok=False)  # Existing output is never truncated.
    assert target.read_bytes() == saved
    run(source, str(root) + "/./input.wav", ok=False)
    assert source.read_bytes() == original
    bad = root / "bad.wav"
    bad.write_bytes(b"RIFF\xff\xff\xff\xffWAVEfmt ")
    run(bad, root / "should-not-exist.wav", ok=False)
    assert not (root / "should-not-exist.wav").exists()
    run("--bad-option", ok=False)
print("WAV CLI: partial frames, sample format, malformed input and no-clobber passed")
