"""Keep provider execution, network authentication and private daemon APIs out of app targets."""
from pathlib import Path
import re

source = Path(__file__).resolve().parents[2]
shared = source / 'shared'
daemon = source / 'daemon'
private_headers = {p.name for p in daemon.glob('*.h')}
private_sources = {p.name for p in daemon.glob('*') if p.suffix in ('.c', '.m')}
for platform in ('iOS', 'macOS'):
    directory = source / platform
    manifest = (directory / 'Makefile').read_text().replace('\\\n', ' ')
    app_sources = []
    for variable in ('SOURCES', 'EXTRA_SOURCES'):
        value = re.search(rf'^{variable} = (.*)$', manifest, re.M)
        assert value, variable
        app_sources += value[1].replace('\\\n', ' ').split()
    seen = set()
    def inspect(path):
        path = path.resolve()
        if path in seen:
            return
        seen.add(path)
        assert daemon not in path.parents, path
        text = path.read_text()
        for name in re.findall(r'^\s*#(?:include|import)\s*["<]([^">]+)[">]', text, re.M):
            assert Path(name).name not in private_headers, (path, name)
            assert name not in ('curl/curl.h', 'Security/Security.h'), (path, name)
            for parent in (path.parent, shared, shared / 'XPFoundation', directory):
                target = parent / name
                if target.is_file():
                    inspect(target)
                    break
    for name in app_sources:
        # Cocoa class StrappySession is intentionally a different UI bridge.
        assert name not in private_sources or name == 'StrappySession.m', name
        candidates = [directory / name, shared / name, shared / 'XPFoundation' / name]
        path = next((p for p in candidates if p.is_file()), None)
        assert path, name
        inspect(path)
    print(f'{platform}: {len(app_sources)} app sources, {len(seen)} files checked; no daemon/network/auth implementation')
